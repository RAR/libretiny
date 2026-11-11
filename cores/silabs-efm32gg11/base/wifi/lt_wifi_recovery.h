/* Copyright (c) Andrew Rankin 2026-06-12. */
// Pure policy for restarting the WF200 after a firmware exception: how many
// restarts, how long to wait before each, when to give up, and what to
// restore afterwards. No hardware, FreeRTOS or lwIP dependencies, so it is
// unit-tested on the host (cores/silabs-efm32gg11/test/wifi). The WiFi
// library (WiFiEvents.cpp) feeds it from its event task and carries out the
// restart itself (lt_wfx_host_teardown() + lt_wfx_host_chip_init()).
//
// Restarts are counted until the chip has run for LT_WIFI_RECOVERY_STABLE_MS
// after one: a chip that faults again within that window uses up another
// attempt, and after LT_WIFI_RECOVERY_MAX_ATTEMPTS the radio is left stopped
// until reboot. So a chip that keeps crashing is restarted at most that many
// times per window, with growing delays, and never in a tight loop.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LT_WIFI_RECOVERY_MAX_ATTEMPTS
#define LT_WIFI_RECOVERY_MAX_ATTEMPTS 3
#endif
// Delay before the first restart; each later one waits four times longer,
// capped at LT_WIFI_RECOVERY_MAX_DELAY_MS (2 s, 8 s, 32 s by default).
#ifndef LT_WIFI_RECOVERY_DELAY_MS
#define LT_WIFI_RECOVERY_DELAY_MS 2000u
#endif
#ifndef LT_WIFI_RECOVERY_MAX_DELAY_MS
#define LT_WIFI_RECOVERY_MAX_DELAY_MS 60000u
#endif
#ifndef LT_WIFI_RECOVERY_STABLE_MS
#define LT_WIFI_RECOVERY_STABLE_MS 600000u
#endif

typedef enum {
	LT_WIFI_RECOVERY_IDLE = 0, // the chip is running (or never faulted)
	LT_WIFI_RECOVERY_PENDING,  // faulted; a restart is due at due_ms
	LT_WIFI_RECOVERY_GAVE_UP,  // out of attempts; stopped until reboot
} lt_wifi_recovery_state_t;

typedef struct {
	lt_wifi_recovery_state_t state;
	uint8_t attempts; // restarts since the chip last ran for LT_WIFI_RECOVERY_STABLE_MS
	uint32_t due_ms;  // PENDING: when the next restart may start
	uint32_t up_ms;	  // when the last restart brought the chip up
} lt_wifi_recovery_t;

void lt_wifi_recovery_init(lt_wifi_recovery_t *r);
// The chip faulted at now_ms. True if a restart is scheduled, false if the
// attempts are used up (state GAVE_UP).
bool lt_wifi_recovery_fault(lt_wifi_recovery_t *r, uint32_t now_ms);
// Whether a scheduled restart should start now.
bool lt_wifi_recovery_due(const lt_wifi_recovery_t *r, uint32_t now_ms);
// A restart finished at now_ms. True if the chip is up (ok) or another
// restart is scheduled; false if that was the last attempt (GAVE_UP).
bool lt_wifi_recovery_done(lt_wifi_recovery_t *r, uint32_t now_ms, bool ok);
// Delay before restart number attempts + 1.
uint32_t lt_wifi_recovery_delay(uint8_t attempts);

// What a successful restart restores. The station is given the status a
// lost link has. If the auto-reconnect supervisor would rejoin it, the rejoin
// is started at once, before the softAP, so that the AP is started on the
// station's channel: the same order of chip commands as begin() followed by
// softAP(). Otherwise the AP is started on its own channel and the station
// is left to the application, as after any link loss.
typedef struct {
	int sta_status;	 // LT_WIFI_RC_LOST if the station had connected (armed), else LT_WIFI_RC_DISCONNECTED
	bool rejoin_sta; // rejoin now (armed, auto-reconnect on)
	bool restart_ap; // the softAP was configured and up: start it again with the stored settings, after the rejoin
} lt_wifi_restore_t;

lt_wifi_restore_t lt_wifi_recovery_plan(bool sta_armed, bool auto_reconnect, bool ap_configured);

#ifdef __cplusplus
}
#endif
