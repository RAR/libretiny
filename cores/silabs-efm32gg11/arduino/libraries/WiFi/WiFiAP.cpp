/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "WiFiPrivate.h"

// softAP. mode()/enableAP() only bring the radio up; softAP() starts the AP
// with the configuration stored in DATA->ap. Addresses are network-order
// ip4_addr_t.addr words, the form (uint32_t)IPAddress yields.

#define LT_WIFI_AP_START_TIMEOUT_MS 5000
// Longest softAP() waits for a pending station join to learn its channel.
// Most joins finish well within it; together with the start timeout it keeps
// softAP() under ESPHome's 10 s loop watchdog.
#define LT_WIFI_AP_JOIN_WAIT_MS		4000
#define LT_WIFI_AP_MAX_CLIENTS		8 // WF200 limit, and the DHCP server's lease count

static const uint32_t AP_DEFAULT_IP	  = PP_HTONL(LWIP_MAKEU32(192, 168, 4, 1));
static const uint32_t AP_DEFAULT_MASK = PP_HTONL(LWIP_MAKEU32(255, 255, 255, 0));

// The softAP MAC: the station MAC (read from the chip by sl_wfx_init()) with
// the locally-administered bit set, so the two differ on air. The same value
// goes to the chip and to the AP netif; they must match.
static sl_wfx_mac_address_t apMacAddress() {
	sl_wfx_mac_address_t mac = wfx_context.mac_addr_0;
	mac.octet[0] |= 0x02;
	return mac;
}

void ltWifiApLock(void *data) {
	if (DATA->apLock)
		xSemaphoreTakeRecursive(DATA->apLock, portMAX_DELAY);
}

void ltWifiApUnlock(void *data) {
	if (DATA->apLock)
		xSemaphoreGiveRecursive(DATA->apLock);
}

void ltWifiApStop(void *data) {
	ltWifiApLock(data);
	if (DATA->apUp) {
		if (DATA->stackUp && !DATA->chipFault) {
			// Also when START_AP_IND never came: the chip may be running the
			// AP regardless. A refusal (not running) is harmless.
			WIFI_CMD_TAKE();
			sl_wfx_stop_ap_command();
			WIFI_CMD_GIVE();
		}
		lt_dhcpserver_stop();
		lt_wf200_netif_ap_destroy();
		DATA->apUp = false;
	}
	DATA->apStarted		= false;
	DATA->apClientCount = 0;
	ltWifiApUnlock(data);
}

static bool apSetMac(const sl_wfx_mac_address_t &mac) {
	WIFI_CMD_TAKE();
	sl_status_t st = sl_wfx_set_mac_address(&mac, SL_WFX_SOFTAP_INTERFACE);
	WIFI_CMD_GIVE();
	if (st != SL_STATUS_OK)
		LT_EM(WIFI, "softAP MAC address not accepted: 0x%08lX", (unsigned long)st);
	return st == SL_STATUS_OK;
}

static bool apIndArrived(void *data) {
	return DATA->apIndStatus >= 0;
}

static bool staJoinSettled(void *data) {
	(void)data;
	return !ltWifiStaJoinPending();
}

// Starts the AP on the chip with DATA->ap, after its MAC: START_AP, its
// indication, the client limit. Returns the channel used, 0 on failure.
static uint16_t apChipStart(void *data) {
	WiFiNetworkInfo &cfg = DATA->ap;
	// One radio cannot serve STA and AP on different channels: follow the
	// station while it is associated, else use the requested channel (or 1).
	// begin() returns before its join completes, so a join still pending
	// decides the channel; wait a while for its outcome.
	if (!ltWifiWaitFor(staJoinSettled, data, LT_WIFI_AP_JOIN_WAIT_MS))
		LT_WM(WIFI, "softAP: station join still pending; not following its channel");
	uint16_t ch = DATA->staChannel ? DATA->staChannel : (cfg.channel > 0 ? (uint16_t)cfg.channel : 1);
	bool open	= cfg.password == NULL;
	// PMF exists only on an RSN network; the WF200 rejects START_AP with
	// INVALID_PARAMETER when it is requested on an open one.
	uint8_t mfp = open ? WFM_MGMT_FRAME_PROTECTION_DISABLED : WFM_MGMT_FRAME_PROTECTION_OPTIONAL;

	DATA->apIndStatus = -1;
	WIFI_CMD_TAKE();
	sl_status_t st = sl_wfx_start_ap_command(
		ch,
		(uint8_t *)cfg.ssid,
		(uint32_t)strlen(cfg.ssid),
		cfg.ssidHidden ? 1 : 0,
		0, // client isolation
		open ? WFM_SECURITY_MODE_OPEN : WFM_SECURITY_MODE_WPA2_PSK,
		mfp,
		(const uint8_t *)(open ? "" : cfg.password),
		(uint16_t)(open ? 0 : strlen(cfg.password)),
		NULL,
		0, // beacon IEs
		NULL,
		0 // probe-response IEs
	);
	WIFI_CMD_GIVE();
	if (st != SL_STATUS_OK) {
		LT_EM(WIFI, "start_ap failed: 0x%08lX", (unsigned long)st);
		return 0;
	}

	// The command is only accepted here; START_AP_IND (event task) carries
	// the result.
	ltWifiWaitFor(apIndArrived, data, LT_WIFI_AP_START_TIMEOUT_MS);
	if (DATA->apIndStatus != WFM_STATUS_SUCCESS) {
		if (DATA->apIndStatus < 0)
			LT_EM(WIFI, "softAP start not confirmed by the chip");
		else
			LT_EM(WIFI, "softAP start refused: status %ld", (long)DATA->apIndStatus);
		return 0;
	}

	if (DATA->apMaxClients) {
		// Must follow START_AP, and is lost when the AP stops.
		WIFI_CMD_TAKE();
		st = sl_wfx_set_max_ap_client(DATA->apMaxClients);
		WIFI_CMD_GIVE();
		if (st != SL_STATUS_OK) {
			LT_EM(WIFI, "softAP client limit not accepted: 0x%08lX", (unsigned long)st);
			return 0;
		}
	}
	return ch;
}

static bool apStart(void *data, const char *hostname) {
	WiFiNetworkInfo &cfg = DATA->ap;
	uint32_t ip			 = cfg.localIP ? cfg.localIP : AP_DEFAULT_IP;
	uint32_t mask		 = cfg.subnet ? cfg.subnet : AP_DEFAULT_MASK;

	sl_wfx_mac_address_t mac = apMacAddress();
	if (!apSetMac(mac))
		return false;
	if (!lt_wf200_netif_ap_create(ip, mask, ip /* the AP is its own gateway */, mac.octet)) {
		LT_EM(WIFI, "softAP netif create failed");
		return false;
	}
	if (hostname && hostname[0]) {
		LOCK_TCPIP_CORE();
		netif_set_hostname(lt_wf200_netif_ap(), hostname);
		UNLOCK_TCPIP_CORE();
	}
	lt_dhcpserver_start(ip, mask);
	DATA->apUp = true;

	uint16_t ch = apChipStart(data);
	if (!ch) {
		ltWifiApStop(data);
		return false;
	}
	LT_IM(WIFI, "softAP %s up on channel %u", cfg.ssid, ch);
	return true;
}

void ltWifiApRestore(void *data) {
	// The netif, its address and the DHCP server were kept across the
	// restart; only the chip lost the AP. The chip's AP MAC must match the
	// netif's. The caller holds the AP lock.
	struct netif *nif = lt_wf200_netif_ap();
	if (!DATA->apUp || !nif || !DATA->ap.ssid)
		return; // torn down meanwhile
	sl_wfx_mac_address_t mac;
	memcpy(mac.octet, nif->hwaddr, 6);
	lt_dhcpserver_release_all();
	lt_wf200_netif_ap_set_link(true);
	uint16_t ch = apSetMac(mac) ? apChipStart(data) : 0;
	if (ch) {
		LT_IM(WIFI, "softAP %s restarted on channel %u", DATA->ap.ssid, ch);
		return;
	}
	LT_EM(WIFI, "softAP not restarted after the WF200 restart; call softAP() again");
	ltWifiApStop(data);
}

bool WiFiClass::softAP(const char *ssid, const char *passphrase, int channel, bool ssidHidden, int maxClients) {
	if (!enableAP(true))
		return false;
	if (passphrase && !*passphrase)
		passphrase = NULL; // an empty passphrase means an open AP
	if (!validate(ssid, passphrase))
		return false;
	if (channel < 0 || channel > 13 || maxClients < 0 || maxClients > LT_WIFI_AP_MAX_CLIENTS) {
		LT_WM(WIFI, "softAP channel %d / max clients %d not supported", channel, maxClients);
		return false;
	}
	if (!DATA->stackUp)
		return false;
	char *newSsid	  = strdup(ssid);
	char *newPassword = passphrase ? strdup(passphrase) : NULL;
	if (!newSsid || (passphrase && !newPassword)) {
		free(newSsid);
		free(newPassword);
		return false;
	}

	ltWifiApLock(data);
	bool ok = false;
	if (DATA->chipFault) {
		ltWifiLogChipDown();
		free(newSsid);
		free(newPassword);
	} else {
		// A running AP is restarted with the new configuration.
		ltWifiApStop(data);
		WiFiNetworkInfo &cfg = DATA->ap;
		free(cfg.ssid);
		free(cfg.password);
		cfg.ssid		   = newSsid;
		cfg.password	   = newPassword;
		cfg.channel		   = channel;
		cfg.ssidHidden	   = ssidHidden;
		DATA->apMaxClients = (uint8_t)maxClients;
		ok				   = apStart(data, apHostname);
	}
	ltWifiApUnlock(data);
	return ok;
}

bool WiFiClass::softAPConfig(IPAddress localIP, IPAddress gateway, IPAddress subnet) {
	uint32_t ip	  = (uint32_t)localIP;
	uint32_t gw	  = (uint32_t)gateway;
	uint32_t mask = (uint32_t)subnet;
	if (!ip) {
		ip	 = AP_DEFAULT_IP;
		gw	 = 0;
		mask = AP_DEFAULT_MASK;
	}
	if (!mask)
		mask = AP_DEFAULT_MASK;
	// The DHCP server always advertises the AP address as the router.
	if (gw && gw != ip) {
		LT_WM(WIFI, "softAP gateway must be the AP address");
		return false;
	}
	// It leases <ip>.10-.17 within the AP address's first three octets, so the
	// subnet must be /24 or wider and the AP address outside that range and
	// not the network or broadcast address.
	uint32_t hip	= lwip_ntohl(ip);
	uint32_t hmask	= lwip_ntohl(mask);
	uint32_t host	= hip & 0xFFU;
	bool contiguous = ((~hmask + 1) & ~hmask) == 0;
	if (!contiguous || hmask > 0xFFFFFF00U || (host >= 10 && host <= 17) || (hip & ~hmask) == 0 ||
		(hip | hmask) == 0xFFFFFFFFU) {
		LT_WM(WIFI, "softAP address/subnet not usable with the DHCP lease range .10-.17");
		return false;
	}
	ltWifiApLock(data);
	bool ok = true;
	if (DATA->apUp && DATA->chipFault) {
		// The AP cannot be restarted now, and the WF200 restart brings it
		// back on its current netif and address.
		ltWifiLogChipDown();
		ok = false;
	} else {
		DATA->ap.localIP = ip;
		DATA->ap.gateway = ip;
		DATA->ap.subnet	 = mask;
		if (DATA->apUp) {
			ltWifiApStop(data);
			ok = apStart(data, apHostname);
		}
	}
	ltWifiApUnlock(data);
	return ok;
}

bool WiFiClass::softAPdisconnect(bool wifiOff) {
	ltWifiApLock(data);
	ltWifiApStop(data);
	free(DATA->ap.ssid);
	free(DATA->ap.password);
	DATA->ap.ssid	  = NULL;
	DATA->ap.password = NULL;
	ltWifiApUnlock(data);
	if (wifiOff)
		return enableAP(false);
	return true;
}

uint8_t WiFiClass::softAPgetStationNum() {
	return DATA->apClientCount;
}

IPAddress WiFiClass::softAPIP() {
	return IPAddress(DATA->ap.localIP ? DATA->ap.localIP : AP_DEFAULT_IP);
}

IPAddress WiFiClass::softAPSubnetMask() {
	return IPAddress(DATA->ap.subnet ? DATA->ap.subnet : AP_DEFAULT_MASK);
}

const char *WiFiClass::softAPgetHostname() {
	return apHostname;
}

bool WiFiClass::softAPsetHostname(const char *hostname) {
	storeHostname(apHostname, hostname);
	struct netif *nif = lt_wf200_netif_ap();
	if (nif) {
		LOCK_TCPIP_CORE();
		netif_set_hostname(nif, apHostname[0] ? apHostname : NULL);
		UNLOCK_TCPIP_CORE();
	}
	return true;
}

uint8_t *WiFiClass::softAPmacAddress(uint8_t *mac) {
	// Zeros until the radio is up (the MAC is derived from the chip's).
	if (DATA->stackUp)
		memcpy(mac, apMacAddress().octet, 6);
	else
		memset(mac, 0, 6);
	return mac;
}

String WiFiClass::softAPmacAddress(void) {
	uint8_t mac[6];
	softAPmacAddress(mac);
	return macToString(mac);
}

const String WiFiClass::softAPSSID(void) {
	return DATA->ap.ssid ? String(DATA->ap.ssid) : String();
}
