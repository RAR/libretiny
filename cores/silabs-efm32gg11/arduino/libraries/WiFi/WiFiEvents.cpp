/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "WiFiPrivate.h"

static_assert(LT_WIFI_RC_IDLE == WL_IDLE_STATUS, "lt_wifi_reconnect.h out of sync with WiFiType.h");
static_assert(LT_WIFI_RC_NO_SSID == WL_NO_SSID_AVAIL, "lt_wifi_reconnect.h out of sync with WiFiType.h");
static_assert(LT_WIFI_RC_CONNECTED == WL_CONNECTED, "lt_wifi_reconnect.h out of sync with WiFiType.h");
static_assert(LT_WIFI_RC_FAILED == WL_CONNECT_FAILED, "lt_wifi_reconnect.h out of sync with WiFiType.h");
static_assert(LT_WIFI_RC_LOST == WL_CONNECTION_LOST, "lt_wifi_reconnect.h out of sync with WiFiType.h");
static_assert(LT_WIFI_RC_DISCONNECTED == WL_DISCONNECTED, "lt_wifi_reconnect.h out of sync with WiFiType.h");
static_assert(LT_WIFI_RC_NO_SHIELD == WL_NO_SHIELD, "lt_wifi_reconnect.h out of sync with WiFiType.h");

// The WiFi event task receives the host port's indications and the netif's
// address changes, keeps the station state, and once a second runs the join
// timer, the auto-reconnect supervisor, the scan timeout and, after a WF200
// firmware exception, the chip restart. It calls the application's onEvent()
// handlers for everything the chip or lwIP reports. Those handlers run on
// this task, not on the bus or tcpip thread, so they may use sockets and DNS.
// They must not call a synchronous scanNetworks(): it waits for this task and
// times out. (softAP() may be called: on this task it services the event
// queue itself while it waits for the chip.) Events caused directly by an API
// call (READY, STA_START, STA_STOP, and STA_DISCONNECTED from begin() or
// disconnect() leaving a connection) are posted synchronously on the calling
// task.
#if LT_WIFI_CHIP_RECOVERY
// A restart runs sl_wfx_init() here, including the Secure Link key exchange
// (X25519 and DRBG seeding, about 2 KiB deep).
#define LT_WIFI_EVENT_TASK_STACK 1536 // words
#else
#define LT_WIFI_EVENT_TASK_STACK 1024 // words
#endif
#define LT_WIFI_EVENT_TASK_PRIO 4
#define LT_WIFI_TICK_MS			1000u

// Right after the host left a network or cancelled a join (begin() or
// disconnect() just before), the chip may still be finishing that and refuse
// the next join with WRONG_STATE: a join is accepted only while the station
// interface is idle. Such a refusal is retried this often, this far apart.
#define LT_WIFI_JOIN_BUSY_RETRIES 5
#define LT_WIFI_JOIN_BUSY_WAIT_MS 100

static TaskHandle_t s_eventTask;
// Guards DATA->staState, DATA->sta (stored credentials), the BSSID/channel
// stash, the scan results and state, and the supervisor variables below.
// Never held across a driver command, an lwIP call or an event handler. When
// both are needed, the command lock is taken first.
static SemaphoreHandle_t s_staLock;

static volatile bool s_autoReconnect = true; // ESP32 parity: on by default
static bool s_armed;						 // reached WL_CONNECTED since the last begin()/disconnect()
static uint32_t s_lastJoinMs;				 // start of the attempt being timed; 0 = none
static uint32_t s_backoffMs;

#if LT_WIFI_CHIP_RECOVERY
// Restart policy state; event task only.
static lt_wifi_recovery_t s_recovery;
// Bound on waiting for a command in flight before a restart. After a fault
// requests fail at once (sl_wfx_host.c), so this only covers a task still
// busy on the bus.
#define LT_WIFI_RECOVERY_LOCK_MS (2 * SL_WFX_DEFAULT_REQUEST_TIMEOUT_MS)
#endif

void ltWifiStaLock() {
	if (s_staLock) // NULL before bring-up, when no other task touches the state
		xSemaphoreTake(s_staLock, portMAX_DELAY);
}

void ltWifiStaUnlock() {
	if (s_staLock)
		xSemaphoreGive(s_staLock);
}

static inline bool radioReady() {
	return pDATA->stackUp && !pDATA->chipFault;
}

static void handleEvent(const lt_wfx_event_t &ev);

bool ltWifiWaitFor(bool (*cond)(void *data), void *data, uint32_t timeoutMs) {
	bool onEventTask = s_eventTask != NULL && xTaskGetCurrentTaskHandle() == s_eventTask;
	uint32_t start	 = millis();
	while (!cond(data)) {
		uint32_t waited = millis() - start;
		// A fault answers nothing more; the fault handling ends the waits.
		if (waited >= timeoutMs || lt_wfx_host.chip_fault)
			return cond(data);
		if (onEventTask) {
			// This task delivers the indications cond() depends on.
			lt_wfx_event_t ev;
			uint32_t left = timeoutMs - waited;
			if (xQueueReceive(lt_wfx_host.event_queue, &ev, pdMS_TO_TICKS(left < 10 ? left : 10)) == pdTRUE)
				handleEvent(ev);
		} else {
			vTaskDelay(pdMS_TO_TICKS(10));
		}
	}
	return true;
}

void ltWifiLogChipDown() {
#if LT_WIFI_CHIP_RECOVERY
	if (s_recovery.state != LT_WIFI_RECOVERY_GAVE_UP) {
		LT_WM(WIFI, "WF200 stopped; restart pending");
		return;
	}
#endif
	LT_EM(WIFI, "WF200 stopped; radio unavailable until reboot");
}

// ---- Station state ----------------------------------------------------------------

static LtWifiLedStage ledForStatus(int status) {
	switch (status) {
		case WL_CONNECTED:
			return LT_WIFI_LED_JOINED;
		case WL_IDLE_STATUS:
			return LT_WIFI_LED_JOINING;
		case WL_CONNECT_FAILED:
		case WL_NO_SSID_AVAIL:
			return LT_WIFI_LED_JOIN_FAIL;
		case WL_NO_SHIELD:
			return LT_WIFI_LED_RADIO_FAIL;
		default:
			return LT_WIFI_LED_IDLE;
	}
}

// Posts the Arduino events a transition asked for (outside the station lock)
// and updates the supervisor and the status LED. STA_DISCONNECTED carries
// the stored SSID, `bssid` (the AP the link or attempt was with; NULL for
// none) and `reason`.
static void staPost(unsigned posts, uint8_t reason, const uint8_t *bssid) {
	if (posts == 0 || !pWiFi)
		return;
	uint32_t now = millis();
	int status;
	EventInfo info;
	EventInfo down;
	memset(&info, 0, sizeof(info));
	memset(&down, 0, sizeof(down));
	ltWifiStaLock();
	if (posts & LT_WIFI_STA_POST_GOT_IP) {
		// Eligible for auto-rejoin from now on; a clean connect resets the backoff.
		s_armed		 = true;
		s_backoffMs	 = 0;
		s_lastJoinMs = 0;
	}
	if (posts & LT_WIFI_STA_POST_LOST_IP)
		s_lastJoinMs = now ? now : 1; // time the lease renewal like a join
	status		   = pDATA->staState.status;
	size_t ssidLen = pDATA->sta.ssid ? strlen(pDATA->sta.ssid) : 0;
	if (ssidLen > sizeof(info.wifi_sta_connected.ssid))
		ssidLen = sizeof(info.wifi_sta_connected.ssid);
	if (posts & LT_WIFI_STA_POST_CONNECTED) {
		memcpy(info.wifi_sta_connected.ssid, pDATA->sta.ssid, ssidLen);
		info.wifi_sta_connected.ssid_len = ssidLen;
		memcpy(info.wifi_sta_connected.bssid, pDATA->staBssid, 6);
		info.wifi_sta_connected.channel = (uint8_t)pDATA->staChannel;
	}
	if (posts & LT_WIFI_STA_POST_DISCONNECTED) {
		memcpy(down.wifi_sta_disconnected.ssid, pDATA->sta.ssid, ssidLen);
		down.wifi_sta_disconnected.ssid_len = ssidLen;
		if (bssid)
			memcpy(down.wifi_sta_disconnected.bssid, bssid, 6);
		down.wifi_sta_disconnected.reason = reason;
	}
	ltWifiStaUnlock();

	ltWifiStatusLed(ledForStatus(status));
	if (posts & LT_WIFI_STA_POST_DISCONNECTED)
		pWiFi->postEvent(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, down);
	if (posts & LT_WIFI_STA_POST_CONNECTED) {
		info.wifi_sta_connected.authmode = pWiFi->getEncryption();
		pWiFi->postEvent(ARDUINO_EVENT_WIFI_STA_CONNECTED, info);
	}
	if (posts & LT_WIFI_STA_POST_GOT_IP) {
		struct netif *nif = lt_wf200_netif_sta();
		EventInfo ip;
		memset(&ip, 0, sizeof(ip));
		if (nif) {
			ip.got_ip.ip_changed		   = true;
			ip.got_ip.ip_info.ip.addr	   = netif_ip4_addr(nif)->addr;
			ip.got_ip.ip_info.netmask.addr = netif_ip4_netmask(nif)->addr;
			ip.got_ip.ip_info.gw.addr	   = netif_ip4_gw(nif)->addr;
		}
		pWiFi->postEvent(ARDUINO_EVENT_WIFI_STA_GOT_IP, ip);
	}
	if (posts & LT_WIFI_STA_POST_LOST_IP) {
		EventInfo lost;
		memset(&lost, 0, sizeof(lost));
		pWiFi->postEvent(ARDUINO_EVENT_WIFI_STA_LOST_IP, lost);
	}
}

static void staRefreshIp() {
	bool hasIp = lt_wf200_netif_sta_ip() != 0;
	ltWifiStaLock();
	unsigned posts = lt_wifi_sta_ip_changed(&pDATA->staState, hasIp);
	ltWifiStaUnlock();
	staPost(posts, 0, NULL);
}

// Clears the association details. Call with the station lock held; `bssid`
// receives the AP's address for the STA_DISCONNECTED event.
static void staForgetAp(uint8_t bssid[6]) {
	memcpy(bssid, pDATA->staBssid, 6);
	memset(pDATA->staBssid, 0, sizeof(pDATA->staBssid));
	pDATA->staChannel = 0;
}

// Ends the attempt or link with `status` if the attempt generation is still
// `gen` (UINT32_MAX: unconditionally). refused: the chip did not accept the
// join, so it owes no CONNECT_IND for it. Returns whether it did.
static bool staLocalDownIf(uint32_t gen, WiFiStatus status, bool refused = false) {
	uint8_t bssid[6];
	uint32_t now = millis();
	ltWifiStaLock();
	bool current   = gen == UINT32_MAX || pDATA->staState.gen == gen;
	unsigned posts = 0;
	if (current) {
		if (refused)
			lt_wifi_sta_join_refused(&pDATA->staState, (int)status);
		else
			posts = lt_wifi_sta_local_down(&pDATA->staState, (int)status, now);
		staForgetAp(bssid);
	}
	ltWifiStaUnlock();
	if (!current)
		return false;
	staPost(posts, WIFI_REASON_ASSOC_LEAVE, bssid);
	ltWifiStatusLed(ledForStatus(status));
	return true;
}

void ltWifiStaLocalDown(WiFiStatus status) {
	staLocalDownIf(UINT32_MAX, status);
}

bool ltWifiStaBusy() {
	ltWifiStaLock();
	bool busy = pDATA->staState.link_up || pDATA->staState.assoc_pending || pDATA->staState.join_pending;
	ltWifiStaUnlock();
	return busy;
}

bool ltWifiStaLinkUp() {
	ltWifiStaLock();
	bool up = pDATA->staState.link_up;
	ltWifiStaUnlock();
	return up;
}

bool ltWifiStaJoinPending() {
	ltWifiStaLock();
	bool pending = pDATA->staState.join_pending;
	ltWifiStaUnlock();
	return pending;
}

// Call with the station lock held.
static bool copyJoinParams(LtWifiJoinParams &params) {
	memset(&params, 0, sizeof(params));
	WiFiNetworkInfo &info = pDATA->sta;
	if (info.ssid == NULL)
		return false;
	strncpy(params.ssid, info.ssid, sizeof(params.ssid) - 1);
	params.open = info.password == NULL;
	if (info.password)
		strncpy(params.password, info.password, sizeof(params.password) - 1);
	params.channel	= info.channel > 0 ? (uint16_t)info.channel : 0;
	params.hasBssid = info.bssid != NULL;
	if (info.bssid)
		memcpy(params.bssid, info.bssid, 6);
	return true;
}

bool ltWifiStaJoinParams(LtWifiJoinParams &params) {
	ltWifiStaLock();
	bool ok = copyJoinParams(params);
	ltWifiStaUnlock();
	return ok;
}

// A join command is about to be sent: new attempt, timed from `now` by the
// join timer. Call with the station lock held; returns the attempt's
// generation.
static uint32_t staJoinStarted(uint32_t now) {
	lt_wifi_sta_join_started(&pDATA->staState);
	s_lastJoinMs = now ? now : 1; // 0 means "no attempt"
	return pDATA->staState.gen;
}

// Sends the join command. Call with the command lock held, after
// staJoinStarted().
static sl_status_t sendJoinCommand(const LtWifiJoinParams &params) {
	// WPA2_WPA1_PSK joins WPA2-Personal and WPA-Personal networks. WPA3-only
	// networks need WFM_SECURITY_MODE_WPA3_SAE and are not supported here.
	sl_wfx_security_mode_t security = params.open ? WFM_SECURITY_MODE_OPEN : WFM_SECURITY_MODE_WPA2_WPA1_PSK;
	return sl_wfx_send_join_command(
		(const uint8_t *)params.ssid,
		strlen(params.ssid),
		params.hasBssid ? (const sl_wfx_mac_address_t *)params.bssid : NULL,
		params.channel, // 0 = any
		security,
		1, // prevent_roaming
		WFM_MGMT_FRAME_PROTECTION_OPTIONAL,
		(const uint8_t *)params.password,
		(uint16_t)strlen(params.password),
		NULL,
		0
	);
}

bool ltWifiSendJoin(const LtWifiJoinParams &params) {
	// The command lock is held from the state change to the command, so the
	// supervisor cannot interleave a join of its own.
	WIFI_CMD_TAKE();
	ltWifiStaLock();
	uint32_t gen = staJoinStarted(millis());
	ltWifiStaUnlock();
	ltWifiStatusLed(LT_WIFI_LED_JOINING);
	sl_status_t st = sendJoinCommand(params);
	for (unsigned n = 0; st == SL_STATUS_WIFI_WRONG_STATE && n < LT_WIFI_JOIN_BUSY_RETRIES; n++) {
		vTaskDelay(pdMS_TO_TICKS(LT_WIFI_JOIN_BUSY_WAIT_MS));
		ltWifiStaLock();
		bool current = pDATA->staState.gen == gen; // not ended by disconnect() meanwhile
		ltWifiStaUnlock();
		if (!current)
			break;
		st = sendJoinCommand(params);
	}
	WIFI_CMD_GIVE();
	if (st != SL_STATUS_OK) {
		LT_EM(WIFI, "Join command failed: 0x%08lX", (unsigned long)st);
		// Without a confirmation the chip may still have taken the join.
		staLocalDownIf(gen, WL_CONNECT_FAILED, st != SL_STATUS_TIMEOUT);
		return false;
	}
	return true;
}

// Driver status codes to Arduino disconnect reasons.
static uint8_t connectFailReason(uint32_t status) {
	switch (status) {
		case WFM_STATUS_NO_MATCHING_AP:
			return WIFI_REASON_NO_AP_FOUND;
		case WFM_STATUS_CONNECTION_AUTH_FAILURE:
			return WIFI_REASON_AUTH_FAIL;
		case WFM_STATUS_CONNECTION_TIMEOUT:
			return WIFI_REASON_HANDSHAKE_TIMEOUT;
		case WFM_STATUS_CONNECTION_REJECTED_BY_AP:
			return WIFI_REASON_ASSOC_FAIL;
		default:
			return WIFI_REASON_CONNECTION_FAIL;
	}
}

static lt_wifi_sta_join_t joinResult(uint32_t status) {
	switch (status) {
		case WFM_STATUS_SUCCESS:
			return LT_WIFI_STA_JOIN_OK;
		case WFM_STATUS_NO_MATCHING_AP:
			return LT_WIFI_STA_JOIN_NO_AP;
		case WFM_STATUS_CONNECTION_ABORTED:
			return LT_WIFI_STA_JOIN_ABORTED;
		default:
			return LT_WIFI_STA_JOIN_FAILED;
	}
}

static uint8_t disconnectReason(uint16_t reason) {
	switch (reason) {
		case WFM_DISCONNECTED_REASON_AP_LOST:
			return WIFI_REASON_BEACON_TIMEOUT;
		case WFM_DISCONNECTED_REASON_REJECTED:
			return WIFI_REASON_AUTH_LEAVE;
		case WFM_DISCONNECTED_REASON_LEAVING_BSS:
			return WIFI_REASON_ASSOC_LEAVE;
		case WFM_DISCONNECTED_REASON_WPA_COUNTERMEASURES:
			return WIFI_REASON_MIC_FAILURE;
		default:
			return WIFI_REASON_UNSPECIFIED;
	}
}

static void onConnectInd(const lt_wfx_event_t &ev) {
	lt_wifi_sta_join_t result = joinResult(ev.u.connect.status);
	uint32_t now			  = millis();
	ltWifiStaLock();
	unsigned posts = lt_wifi_sta_connect_ind(&pDATA->staState, result, now);
	bool dhcp	   = pDATA->sta.localIP == 0;
	if (posts & LT_WIFI_STA_POST_LINK_UP) {
		memcpy(pDATA->staBssid, ev.u.connect.mac, 6);
		pDATA->staChannel = ev.u.connect.channel;
	}
	ltWifiStaUnlock();
	if (posts == 0) {
		// The answer to an attempt the host has ended (and cancelled with a
		// disconnect request, which also drops an association it made), or
		// to none at all. Leaving it alone matters: a disconnect sent now
		// would cancel the join that replaced it.
		LT_DM(WIFI, "Ignoring CONNECT_IND (status %lu) for an ended join", (unsigned long)ev.u.connect.status);
		return;
	}
	if (posts & LT_WIFI_STA_POST_LINK_UP) {
		// lwIP link first, so the address is usable once WL_CONNECTED is
		// published (at once for a static address).
		lt_wf200_netif_set_link(true, dhcp);
		ltWifiStaLock();
		posts = lt_wifi_sta_link_ready(&pDATA->staState);
		ltWifiStaUnlock();
		if (posts == 0) {
			// The host ended the attempt meanwhile and is disconnecting it.
			lt_wf200_netif_set_link(false, false);
			return;
		}
	} else {
		LT_WM(WIFI, "Join failed: status %lu", (unsigned long)ev.u.connect.status);
	}
	staPost(posts, connectFailReason(ev.u.connect.status), ev.u.connect.mac);
}

static void onDisconnectInd(const lt_wfx_event_t &ev) {
	uint8_t bssid[6];
	ltWifiStaLock();
	unsigned posts = lt_wifi_sta_disconnect_ind(&pDATA->staState);
	if (posts)
		staForgetAp(bssid);
	ltWifiStaUnlock();
	if (!posts)
		return; // the host had already taken the link down
	LT_IM(WIFI, "Disconnected: reason %u", ev.u.disconnect.reason);
	lt_wf200_netif_set_link(false, false);
	staPost(posts, disconnectReason(ev.u.disconnect.reason), bssid);
}

// ---- Join timer and supervisor ----------------------------------------------------

void ltWifiSetAutoReconnect(bool enable) {
	s_autoReconnect = enable;
}

bool ltWifiGetAutoReconnect() {
	return s_autoReconnect;
}

void ltWifiReconnectDisarm() {
	ltWifiStaLock();
	s_armed		 = false;
	s_backoffMs	 = 0;
	s_lastJoinMs = 0;
	ltWifiStaUnlock();
}

// Gives up on the current attempt or link because it failed: disconnects,
// publishes `status` and posts STA_DISCONNECTED (also for a join that never
// associated). Only if the attempt generation is still `gen` (and, with
// requireArmed, the supervisor still armed): an attempt the application
// started since the decision is left alone.
static void staEndAttempt(const char *why, uint32_t gen, WiFiStatus status, bool requireArmed) {
	uint8_t bssid[6];
	WIFI_CMD_TAKE();
	ltWifiStaLock();
	bool go		   = pDATA->staState.gen == gen && (!requireArmed || s_armed);
	unsigned posts = 0;
	if (go) {
		posts = lt_wifi_sta_attempt_failed(&pDATA->staState, (int)status, millis());
		staForgetAp(bssid);
	}
	ltWifiStaUnlock();
	// Cancels a pending join (the chip answers with CONNECT_IND ABORTED,
	// which is ignored) or leaves the association.
	if (go)
		sl_wfx_send_disconnect_command();
	WIFI_CMD_GIVE();
	if (!go)
		return;
	LT_WM(WIFI, "%s", why);
	lt_wf200_netif_set_link(false, false);
	staPost(posts, WIFI_REASON_CONNECTION_FAIL, bssid);
}

static void supervisorJoin(uint32_t now, uint32_t gen) {
	LtWifiJoinParams params;
	uint32_t joinGen = 0;
	sl_status_t st	 = SL_STATUS_OK;
	// Check, state change and command under the command lock, so neither an
	// application join nor begin()/disconnect() can come in between.
	WIFI_CMD_TAKE();
	ltWifiStaLock();
	bool go = s_armed && pDATA->staState.gen == gen && copyJoinParams(params);
	if (go) {
		s_backoffMs = lt_wifi_rc_next_backoff(s_backoffMs);
		joinGen		= staJoinStarted(now);
	}
	ltWifiStaUnlock();
	if (go) {
		LT_IM(WIFI, "Auto-reconnect: rejoining %s", params.ssid);
		ltWifiStatusLed(LT_WIFI_LED_JOINING);
		st = sendJoinCommand(params);
	}
	WIFI_CMD_GIVE();
	if (go && st != SL_STATUS_OK) {
		LT_EM(WIFI, "Join command failed: 0x%08lX", (unsigned long)st);
		staLocalDownIf(joinGen, WL_CONNECT_FAILED, st != SL_STATUS_TIMEOUT);
	}
}

static void supervisorTick(uint32_t now) {
	if (!radioReady())
		return;
	ltWifiStaLock();
	lt_wifi_rc_input_t in = {
		.enabled	  = s_autoReconnect,
		.armed		  = s_armed,
		.have_ssid	  = pDATA->sta.ssid != NULL,
		.status		  = pDATA->staState.status,
		.now_ms		  = now,
		.last_join_ms = s_lastJoinMs,
		.backoff_ms	  = s_backoffMs,
	};
	uint32_t gen = pDATA->staState.gen;
	ltWifiStaUnlock();
	switch (lt_wifi_rc_decide(&in)) {
		case LT_WIFI_RC_ACTION_JOIN:
			supervisorJoin(now, gen);
			break;
		case LT_WIFI_RC_ACTION_DEMOTE:
			staEndAttempt("No connection within the join window; retrying", gen, WL_CONNECTION_LOST, true);
			break;
		case LT_WIFI_RC_ACTION_FAIL:
			staEndAttempt("No connection within the join window", gen, WL_CONNECT_FAILED, false);
			break;
		default:
			break;
	}
}

// ---- Scan timeout -----------------------------------------------------------------

// Ends a running scan without its SCAN_COMPLETE: marks it ended (a
// synchronous scanNetworks() then returns WIFI_SCAN_FAILED), releases the
// waiter and posts SCAN_DONE with status 1. Returns whether a scan ran.
static bool scanEnd() {
	ltWifiStaLock();
	WiFiScanData *scan = pWiFi->scan;
	bool running	   = scan && scan->running;
	uint8_t count	   = 0;
	if (running) {
		scan->running		= false;
		pDATA->scanTimedOut = true;
		count				= scan->count;
	}
	ltWifiStaUnlock();
	if (!running)
		return false;
	xSemaphoreGive(pDATA->scanSem);
	EventInfo info;
	memset(&info, 0, sizeof(info));
	info.wifi_scan_done.status = 1;
	info.wifi_scan_done.number = count;
	pWiFi->postEvent(ARDUINO_EVENT_WIFI_SCAN_DONE, info);
	return true;
}

// A scan whose SCAN_COMPLETE never arrives (dropped, or the chip stalled) is
// ended here once its deadline (scan->timeout) has passed, so later scans are
// not refused as running. The chip's SCAN_COMPLETE for the stop request is
// swallowed (scanStale), along with any late results before it.
static void scanCheckTimeout(uint32_t now) {
	ltWifiStaLock();
	WiFiScanData *scan = pWiFi->scan;
	bool expired	   = scan && scan->running && (int32_t)(now - (uint32_t)scan->timeout) >= 0;
	ltWifiStaUnlock();
	if (!expired)
		return;
	LT_WM(WIFI, "Scan timed out");
	if (radioReady()) {
		WIFI_CMD_TAKE();
		sl_status_t st = sl_wfx_send_stop_scan_command();
		WIFI_CMD_GIVE();
		if (st == SL_STATUS_OK) {
			ltWifiStaLock();
			pDATA->scanStale   = 1;
			pDATA->scanStaleMs = now;
			ltWifiStaUnlock();
		}
	}
	scanEnd();
}

// ---- Firmware exception and restart -----------------------------------------------
//
// EXCEPTION_IND means the WF200 firmware has stopped; it answers nothing more.
// The host port latches it (lt_wfx_host.chip_fault) and fails every request
// from then on. Here the station and AP are reported down as after any link
// loss (the netifs stay; their links go down, so sockets fail and the
// application reconnects as usual), and waits on the chip are ended. With
// LT_WIFI_CHIP_RECOVERY the chip is then restarted from this task, a bounded
// number of times (lt_wifi_recovery.h), and the station and softAP are
// restored; without it, or once the attempts are used up, the radio stays
// stopped (WL_NO_SHIELD) until reboot.

// Without the AP lock: an application softAP() holding it may be waiting
// for START_AP_IND, and this releases it. Racing a teardown of the AP netif
// is harmless (it is static; a removed netif just changes link state).
static void apChipDown() {
	bool wasStarted		 = pDATA->apStarted;
	pDATA->apStarted	 = false;
	pDATA->apClientCount = 0;
	if (pDATA->apIndStatus < 0)
		pDATA->apIndStatus = WFM_STATUS_GENERAL_FAILURE; // softAP() waiting for START_AP_IND gives up
	if (pDATA->apUp) {
		lt_wf200_netif_ap_set_link(false);
		lt_dhcpserver_release_all(); // the clients are gone, unannounced
	}
	if (wasStarted) {
		EventInfo info;
		memset(&info, 0, sizeof(info));
		pWiFi->postEvent(ARDUINO_EVENT_WIFI_AP_STOP, info);
	}
}

static void staChipDown() {
	uint8_t bssid[6];
	lt_wf200_netif_set_link(false, false);
	ltWifiStaLock();
	// The supervisor stays armed: a restarted chip rejoins a lost connection.
	unsigned posts = lt_wifi_sta_chip_down(&pDATA->staState);
	staForgetAp(bssid);
	ltWifiStaUnlock();
	staPost(posts, WIFI_REASON_UNSPECIFIED, bssid);
}

static void chipGiveUp(const char *why) {
	LT_EM(WIFI, "%s; WF200 radio stopped until reboot", why);
	ltWifiReconnectDisarm();
	ltWifiStatusLed(LT_WIFI_LED_RADIO_FAIL);
}

static void onChipException(uint32_t reason) {
	LT_EM(WIFI, "WF200 firmware exception, reason %lu", (unsigned long)reason);
	pDATA->chipFault = true;
	staChipDown();
	scanEnd();
	apChipDown();
#if LT_WIFI_CHIP_RECOVERY
	if (lt_wifi_recovery_fault(&s_recovery, millis()))
		LT_IM(WIFI, "Restarting the WF200 in %lu ms", (unsigned long)(s_recovery.due_ms - millis()));
	else
		chipGiveUp("WF200 restarts used up");
#else
	chipGiveUp("No restart (LT_WIFI_CHIP_RECOVERY is 0)");
#endif
}

#if LT_WIFI_CHIP_RECOVERY
// sl_wfx_init() leaves the chip on its factory MAC; an address set with
// setMacAddress() lives on in the STA netif and is set again. Call with the
// command lock held.
static sl_status_t chipRestoreStaMac() {
	struct netif *nif = lt_wf200_netif_sta();
	if (!nif || memcmp(nif->hwaddr, wfx_context.mac_addr_0.octet, 6) == 0)
		return SL_STATUS_OK;
	sl_wfx_mac_address_t addr;
	memcpy(addr.octet, nif->hwaddr, 6);
	return sl_wfx_set_mac_address(&addr, SL_WFX_STA_INTERFACE);
}

// One restart: tear the old driver session down, start a new one (reset,
// firmware download, Secure Link, PDS), then restore the configuration.
static bool chipRestart() {
	LT_IM(
		WIFI,
		"Restarting the WF200 (attempt %u of %u)",
		(unsigned)s_recovery.attempts + 1,
		(unsigned)LT_WIFI_RECOVERY_MAX_ATTEMPTS
	);
	ltWifiStatusLed(LT_WIFI_LED_RADIO_INIT);
	if (!lt_wfx_cmd_lock_timeout(LT_WIFI_RECOVERY_LOCK_MS)) {
		LT_EM(WIFI, "WF200 restart: a driver command did not finish");
		ltWifiStatusLed(LT_WIFI_LED_RADIO_FAIL);
		return false;
	}
	uint32_t startMs = millis();
	sl_status_t st	 = lt_wfx_host_teardown();
	if (st == SL_STATUS_OK)
		st = lt_wfx_host_chip_init();
	if (st == SL_STATUS_OK)
		st = chipRestoreStaMac();
	if (st != SL_STATUS_OK)
		lt_wfx_host_hold_faulted();
	WIFI_CMD_GIVE();
	if (st != SL_STATUS_OK) {
		LT_EM(WIFI, "WF200 restart failed: 0x%08lX", (unsigned long)st);
		ltWifiStatusLed(LT_WIFI_LED_RADIO_FAIL);
		return false;
	}
	LT_IM(
		WIFI,
		"WF200 FW %u.%u.%u up again in %lu ms",
		wfx_context.firmware_major,
		wfx_context.firmware_minor,
		wfx_context.firmware_build,
		(unsigned long)(millis() - startMs)
	);

	// The AP lock is taken before the radio is marked ready, so that the
	// application's softAP calls wait for the restore instead of racing it.
	ltWifiApLock(pDATA);
	// The station is given the status of a lost link (or DISCONNECTED if it
	// had not connected since the last begin()). It was down already, so
	// nothing is posted. See lt_wifi_recovery_plan() for the order.
	ltWifiStaLock();
	lt_wifi_restore_t plan =
		lt_wifi_recovery_plan(s_armed, s_autoReconnect && pDATA->sta.ssid, pDATA->apUp && pDATA->ap.ssid);
	s_lastJoinMs = 0;
	s_backoffMs	 = 0;
	lt_wifi_sta_local_down(&pDATA->staState, plan.sta_status, millis());
	uint32_t gen = pDATA->staState.gen;
	ltWifiStaUnlock();
	pDATA->chipFault = false;
	ltWifiStatusLed(ledForStatus(plan.sta_status));
	// A refused rejoin is retried by the supervisor.
	if (plan.rejoin_sta)
		supervisorJoin(millis(), gen);
	// Waits (briefly) for that join, to follow its channel.
	if (plan.restart_ap)
		ltWifiApRestore(pDATA); // logs its own failure; the station is unaffected
	ltWifiApUnlock(pDATA);
	return true;
}
#endif

static void recoveryTick(uint32_t now) {
#if LT_WIFI_CHIP_RECOVERY
	if (!pDATA->chipFault || !lt_wifi_recovery_due(&s_recovery, now))
		return;
	bool ok = chipRestart();
	if (!lt_wifi_recovery_done(&s_recovery, millis(), ok))
		chipGiveUp("WF200 restart failed");
	else if (!ok)
		LT_WM(WIFI, "Next WF200 restart in %lu ms", (unsigned long)(s_recovery.due_ms - millis()));
#else
	(void)now;
#endif
}

static void checkDropped() {
	uint32_t n = lt_wfx_host_take_dropped(LT_WFX_DROPPED_SCAN);
	if (n)
		LT_WM(WIFI, "%lu scan results dropped (event queue full)", (unsigned long)n);
	n = lt_wfx_host_take_dropped(LT_WFX_DROPPED_OTHER);
	if (n)
		LT_EM(WIFI, "%lu WF200 indications lost (event queue full)", (unsigned long)n);
	n = lt_wfx_host_take_dropped(LT_WFX_DROPPED_STA);
	if (n == 0)
		return;
	// A lost CONNECT/DISCONNECT indication leaves the station state wrong, so
	// restart any attempt or link from a known state.
	LT_EM(WIFI, "%lu station indications lost (event queue full)", (unsigned long)n);
	ltWifiStaLock();
	bool busy	 = pDATA->staState.link_up || pDATA->staState.assoc_pending || pDATA->staState.join_pending;
	uint32_t gen = pDATA->staState.gen;
	ltWifiStaUnlock();
	if (radioReady() && busy)
		staEndAttempt("Station state unknown after lost indications; retrying", gen, WL_CONNECTION_LOST, false);
}

// ---- Event task -------------------------------------------------------------------

// Scan-result security bitmask (raw sl_wfx_security_mode_bitmask_t byte) to
// Arduino auth mode.
WiFiAuthMode wfxSecurityToAuthMode(uint32_t security) {
	sl_wfx_security_mode_bitmask_t bits;
	uint8_t raw = (uint8_t)security;
	memcpy(&bits, &raw, sizeof(bits));
	if (bits.wpa3)
		return bits.wpa2 ? WIFI_AUTH_WPA2_WPA3_PSK : WIFI_AUTH_WPA3_PSK;
	if (bits.wpa2) {
		if (bits.eap && !bits.psk)
			return WIFI_AUTH_WPA2_ENTERPRISE;
		return bits.wpa ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_WPA2_PSK;
	}
	if (bits.wpa)
		return WIFI_AUTH_WPA_PSK;
	if (bits.wep)
		return WIFI_AUTH_WEP;
	return WIFI_AUTH_OPEN;
}

static void handleEvent(const lt_wfx_event_t &ev) {
	EventInfo info;
	memset(&info, 0, sizeof(info));

	switch (ev.id) {
		case SL_WFX_CONNECT_IND_ID:
			onConnectInd(ev);
			break;

		case SL_WFX_DISCONNECT_IND_ID:
			onDisconnectInd(ev);
			break;

		case SL_WFX_SCAN_RESULT_IND_ID: {
			// Stored under the station lock: scanNetworks() deletes and
			// re-creates the result list under the same lock.
			ltWifiStaLock();
			WiFiScanData *scan = pWiFi->scan;
			if (!pDATA->scanStale && scan && scan->running && scan->count < UINT8_MAX) {
				uint8_t last = scan->count + 1;
				if (pWiFi->scanAlloc(last) == last) {
					WiFiScanAP &ap = scan->ap[last - 1];
					ap.ssid		   = strdup((char *)ev.u.scan_result.ssid);
					ap.rssi		   = ev.u.scan_result.rssi_dbm;
					ap.channel	   = (int32_t)ev.u.scan_result.channel;
					ap.auth		   = wfxSecurityToAuthMode(ev.u.scan_result.security);
					memcpy(ap.bssid.addr, ev.u.scan_result.mac, 6);
				}
			}
			ltWifiStaUnlock();
			break;
		}

		case SL_WFX_SCAN_COMPLETE_IND_ID: {
			ltWifiStaLock();
			bool stale = pDATA->scanStale != 0;
			if (stale) {
				pDATA->scanStale--; // completes a scan abandoned by scanCheckTimeout()
			} else if (pWiFi->scan) {
				pWiFi->scan->running	   = false;
				info.wifi_scan_done.number = pWiFi->scan->count;
			}
			ltWifiStaUnlock();
			if (stale)
				break;
			info.wifi_scan_done.status = ev.u.scan_complete.status == 0 ? 0 : 1;
			xSemaphoreGive(pDATA->scanSem);
			pWiFi->postEvent(ARDUINO_EVENT_WIFI_SCAN_DONE, info);
			break;
		}

		case SL_WFX_START_AP_IND_ID:
			// softAP() waits for this status.
			if (ev.u.start_ap.status == WFM_STATUS_SUCCESS)
				pDATA->apStarted = true;
			pDATA->apIndStatus = (int32_t)ev.u.start_ap.status;
			if (pDATA->apStarted)
				pWiFi->postEvent(ARDUINO_EVENT_WIFI_AP_START, info);
			break;

		case SL_WFX_STOP_AP_IND_ID:
			// Host-side AP resources stay until softAPdisconnect()/AP disable
			// tears them down (DATA->apUp).
			pDATA->apStarted	 = false;
			pDATA->apClientCount = 0;
			pWiFi->postEvent(ARDUINO_EVENT_WIFI_AP_STOP, info);
			break;

		case SL_WFX_AP_CLIENT_CONNECTED_IND_ID:
			if (pDATA->apClientCount < UINT8_MAX)
				pDATA->apClientCount++;
			memcpy(info.wifi_ap_staconnected.mac, ev.u.ap_client.mac, 6);
			pWiFi->postEvent(ARDUINO_EVENT_WIFI_AP_STACONNECTED, info);
			break;

		case SL_WFX_AP_CLIENT_DISCONNECTED_IND_ID:
			if (pDATA->apClientCount > 0)
				pDATA->apClientCount--;
			lt_dhcpserver_release(ev.u.ap_client.mac);
			memcpy(info.wifi_ap_stadisconnected.mac, ev.u.ap_client.mac, 6);
			pWiFi->postEvent(ARDUINO_EVENT_WIFI_AP_STADISCONNECTED, info);
			break;

		case SL_WFX_AP_CLIENT_REJECTED_IND_ID:
			LT_DM(WIFI, "softAP rejected a client: reason %u", ev.u.ap_client.reason);
			lt_dhcpserver_release(ev.u.ap_client.mac);
			break;

		case SL_WFX_ERROR_IND_ID:
			// None of the documented error types (sl_wfx_general_error_api.h)
			// is a stopped chip; that is EXCEPTION_IND, handled through
			// chip_fault.
			LT_EM(WIFI, "WF200 error indication, type %lu", (unsigned long)ev.u.fault.code);
			break;

		case LT_WFX_EVENT_SLK_FAILED:
			LT_EM(WIFI, "WF200 Secure Link renegotiation failed: 0x%08lX", (unsigned long)ev.u.fault.code);
			break;

		case LT_WFX_EVENT_IP_CHANGED:
		default:
			break; // the address is re-read after every event
	}
}

static void wifiEventTask(void *arg) {
	(void)arg;
	s_eventTask		  = xTaskGetCurrentTaskHandle(); // before xTaskCreate() has returned it
	uint32_t nextTick = millis() + LT_WIFI_TICK_MS;
	for (;;) {
		lt_wfx_event_t ev;
		int32_t wait = (int32_t)(nextTick - millis());
		bool got	 = xQueueReceive(lt_wfx_host.event_queue, &ev, wait > 0 ? pdMS_TO_TICKS(wait) : 0) == pdTRUE;
		if (pWiFi) {
			// Latched by the host port at indication time (sl_wfx_host.c).
			// Only once the radio is up: a fault during bring-up fails that
			// sl_wfx_init(), and the application's next mode()/begin() starts
			// afresh (lt_wfx_host_prepare_init()).
			if (pDATA->stackUp && lt_wfx_host.chip_fault && !pDATA->chipFault)
				onChipException(lt_wfx_host.fault_reason);
			checkDropped();
			// While the chip is down, whatever it sent before stopping is
			// stale: the station and AP have been taken down already, and a
			// restart discards the rest of the queue.
			if (got && !pDATA->chipFault)
				handleEvent(ev);
			staRefreshIp();
		}
		// The tick runs on schedule even while events keep arriving.
		uint32_t now = millis();
		if ((int32_t)(now - nextTick) >= 0) {
			nextTick = now + LT_WIFI_TICK_MS;
			if (pWiFi) {
				supervisorTick(now);
				scanCheckTimeout(now);
				recoveryTick(now);
			}
		}
	}
}

// Runs in whatever task changed the STA address (the tcpip thread for DHCP),
// with the core lock held. It only wakes the event task, which re-reads the
// address; a full queue is harmless because the task re-reads it at least
// once per tick anyway.
static void netifStatusCb(struct netif *nif) {
	(void)nif;
	lt_wfx_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.id = LT_WFX_EVENT_IP_CHANGED;
	if (lt_wfx_host.event_queue)
		xQueueSend(lt_wfx_host.event_queue, &ev, 0);
}

void ltWifiAttachNetifCallback() {
	struct netif *nif = lt_wf200_netif_sta();
	if (!nif)
		return;
	LOCK_TCPIP_CORE();
	netif_set_status_callback(nif, netifStatusCb);
	UNLOCK_TCPIP_CORE();
}

bool startWifiEventTask() {
	if (lt_wfx_host_setup() != SL_STATUS_OK) {
		LT_EM(WIFI, "WF200 host setup failed");
		return false;
	}
	if (!s_staLock && !(s_staLock = xSemaphoreCreateMutex())) {
		LT_EM(WIFI, "WiFi state lock create failed");
		return false;
	}
	// The WIRQ edge interrupt is attached by the host port when sl_wfx_init()
	// enables it (sl_wfx_host_enable_platform_interrupt()).
	if (!s_eventTask) {
#if LT_WIFI_CHIP_RECOVERY
		lt_wifi_recovery_init(&s_recovery);
#endif
		if (xTaskCreate(
				wifiEventTask,
				"wifievent",
				LT_WIFI_EVENT_TASK_STACK,
				NULL,
				LT_WIFI_EVENT_TASK_PRIO,
				&s_eventTask
			) != pdPASS) {
			LT_EM(WIFI, "WiFi event task create failed");
			s_eventTask = NULL;
			return false;
		}
	}
	return true;
}
