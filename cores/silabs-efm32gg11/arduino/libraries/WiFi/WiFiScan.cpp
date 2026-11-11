/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "WiFiPrivate.h"

// Results arrive as SCAN_RESULT indications and are stored by the WiFi event
// task. The common scanDelete() does not check for a running scan and the
// event task writes into scan->ap until the scan completes, so after an
// asynchronous scan wait for ARDUINO_EVENT_WIFI_SCAN_DONE (or for
// scanComplete() != WIFI_SCAN_RUNNING) before calling scanDelete().

// A scan abandoned on timeout owes one SCAN_COMPLETE (for the stop request);
// after this long it is assumed lost and no longer swallowed.
#define LT_WIFI_SCAN_STALE_MS 2000

int16_t WiFiClass::scanNetworks(bool async, bool showHidden, bool passive, uint32_t maxMsPerChannel, uint8_t channel) {
	(void)showHidden;
	if (scan && scan->running)
		return WIFI_SCAN_RUNNING;
	if (!enableSTA(true))
		return WIFI_SCAN_FAILED;
	if (!DATA->stackUp || DATA->chipFault)
		return WIFI_SCAN_FAILED;

	// The WiFi event task ends the scan once this has passed without
	// SCAN_COMPLETE (scanCheckTimeout() in WiFiEvents.cpp).
	uint32_t timeout = (maxMsPerChannel ? maxMsPerChannel : 300) * 14 + 2000;

	// The event task stores results under the station lock, so the list is
	// replaced under it too.
	ltWifiStaLock();
	scanDelete();
	scanInit();
	bool ok = scan != NULL;
	if (ok) {
		if (DATA->scanStale && millis() - DATA->scanStaleMs > LT_WIFI_SCAN_STALE_MS)
			DATA->scanStale = 0;
		// Set before the command: the first results can arrive before this
		// task runs again, and the event task drops results while it is false.
		scan->running	   = true;
		scan->timeout	   = millis() + timeout;
		DATA->scanTimedOut = false;
	}
	ltWifiStaUnlock();
	if (!ok)
		return WIFI_SCAN_FAILED;

	uint8_t channel_list[1] = {channel};
	LT_IM(WIFI, "Starting WiFi scan");
	xSemaphoreTake(DATA->scanSem, 0); // drain a stale give
	WIFI_CMD_TAKE();
	sl_status_t st = sl_wfx_send_scan_command(
		passive ? WFM_SCAN_MODE_PASSIVE : WFM_SCAN_MODE_ACTIVE,
		channel ? channel_list : NULL,
		channel ? 1 : 0,
		NULL,
		0,
		NULL,
		0,
		NULL
	);
	WIFI_CMD_GIVE();
	if (st != SL_STATUS_OK && st != SL_STATUS_WIFI_WARNING) {
		LT_EM(WIFI, "scan command failed: 0x%08lX", (unsigned long)st);
		ltWifiStaLock();
		scan->running = false;
		ltWifiStaUnlock();
		return WIFI_SCAN_FAILED;
	}

	if (async)
		return WIFI_SCAN_RUNNING;

	// Synchronous: SCAN_COMPLETE, or the event task's timeout, gives scanSem.
	// The extra margin covers the timeout check running once a second.
	if (xSemaphoreTake(DATA->scanSem, pdMS_TO_TICKS(timeout + 2000)) != pdTRUE) {
		LT_EM(WIFI, "scan timed out");
		ltWifiStaLock();
		scan->running = false;
		ltWifiStaUnlock();
		return WIFI_SCAN_FAILED;
	}
	if (DATA->scanTimedOut)
		return WIFI_SCAN_FAILED;
	return scan->count;
}
