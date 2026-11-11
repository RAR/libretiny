/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "WiFiPrivate.h"

// One-time radio and lwIP bring-up, shared by STA and AP enable so that
// sl_wfx_init() runs once. On failure the status is WL_NO_SHIELD and the
// status LED (if enabled) shows the fault. Called through mode(), which has
// set pWiFi.
static bool bringUpRadio(void *data) {
	if (DATA->stackUp)
		return true;

	// lwIP comes up first and regardless of the radio, so sockets used by the
	// application fail cleanly instead of finding no tcpip thread.
	if (!lt_wf200_netif_stack_init()) {
		LT_EM(WIFI, "lwIP init failed");
		return false;
	}

	// Red from here on: a radio that never answers leaves it red, a clean
	// failure turns it magenta.
	ltWifiStatusLed(LT_WIFI_LED_RADIO_INIT);
	// Host primitives and the event task must exist before sl_wfx_init().
	if (!startWifiEventTask()) {
		ltWifiStaLocalDown(WL_NO_SHIELD);
		return false;
	}

	// Runs on the caller: chip reset (35 ms of fixed waits), SDIO
	// enumeration, the 312 KB firmware download, the startup indication, the
	// Secure Link key exchange (two X25519 operations in software) and the
	// PDS. From the code this is estimated at well under 2 s, not measured;
	// every step is bounded by the driver's timeouts. The log line below
	// gives the real figure.
	unsigned long startMs = millis();
	WIFI_CMD_TAKE();
	// A failed earlier attempt may have left a fault latched (an exception
	// during its sl_wfx_init()).
	lt_wfx_host_prepare_init();
	sl_status_t st = lt_wfx_host_chip_init();
	WIFI_CMD_GIVE();
	if (st != SL_STATUS_OK) {
		LT_EM(WIFI, "WF200 init failed: 0x%08lX", (unsigned long)st);
		ltWifiStaLocalDown(WL_NO_SHIELD);
		return false;
	}
	LT_IM(
		WIFI,
		"WF200 FW %u.%u.%u up in %lu ms",
		wfx_context.firmware_major,
		wfx_context.firmware_minor,
		wfx_context.firmware_build,
		millis() - startMs
	);

	if (!lt_wf200_netif_create()) {
		LT_EM(WIFI, "lwIP/netif bring-up failed");
		ltWifiStaLocalDown(WL_NO_SHIELD);
		return false;
	}
	ltWifiAttachNetifCallback();
	DATA->stackUp = true;
	ltWifiStaLocalDown(WL_DISCONNECTED); // radio and IP stack up, not joined
	return true;
}

bool WiFiClass::modePriv(WiFiMode mode, WiFiModeAction sta, WiFiModeAction ap) {
	EventInfo info;
	memset(&info, 0, sizeof(info));

	// Enabling either interface brings the radio up. The softAP itself is
	// started by softAP().
	if ((sta == WLMODE_ENABLE || ap == WLMODE_ENABLE) && !DATA->stackUp) {
		if (!bringUpRadio(data))
			return false;
		struct netif *nif = lt_wf200_netif_sta();
		if (nif && staHostname[0]) {
			LOCK_TCPIP_CORE();
			netif_set_hostname(nif, staHostname);
			UNLOCK_TCPIP_CORE();
		}
		postEvent(ARDUINO_EVENT_WIFI_READY, info);
	}

	if (ap == WLMODE_DISABLE)
		ltWifiApStop(data);

	if (sta == WLMODE_DISABLE && DATA->stackUp) {
		ltWifiReconnectDisarm(); // intentional: no auto-rejoin
		bool busy = ltWifiStaBusy();
		// No STOPPED status in WiFiType.h. A stopped chip stays WL_NO_SHIELD.
		ltWifiStaLocalDown(DATA->chipFault ? WL_NO_SHIELD : WL_DISCONNECTED);
		if (busy && !DATA->chipFault) {
			WIFI_CMD_TAKE();
			sl_wfx_send_disconnect_command();
			WIFI_CMD_GIVE();
		}
		lt_wf200_netif_set_link(false, false);
		postEvent(ARDUINO_EVENT_WIFI_STA_STOP, info);
	}
	if (sta == WLMODE_ENABLE)
		postEvent(ARDUINO_EVENT_WIFI_STA_START, info);

	DATA->mode = mode;
	return true;
}

WiFiMode WiFiClass::getMode() {
	return DATA->mode;
}

WiFiStatus WiFiClass::status() {
	return (WiFiStatus)DATA->staState.status;
}
