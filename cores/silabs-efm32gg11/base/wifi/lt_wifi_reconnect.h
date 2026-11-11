/* Copyright (c) Andrew Rankin 2026-06-12. */
// Pure decision logic of the station join timer and auto-reconnect
// supervisor. No hardware, FreeRTOS or lwIP dependencies, so it is
// unit-tested on the host (cores/silabs-efm32gg11/test/wifi). The WiFi
// library (WiFiEvents.cpp) supplies the inputs once a second on its event
// task and carries out the returned action.
//
// Every join is timed, whoever started it: begin() only issues the join, so
// a join that never completes, or associates but never gets an address, is
// ended here. The WF200 does not rejoin by itself after losing its AP, so the
// host does, for a connection that had reached WL_CONNECTED. Loss is detected
// from driver indications: DISCONNECT_IND (the firmware reports beacon loss
// as WFM_DISCONNECTED_REASON_AP_LOST) and the DHCP lease going away.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// WiFiStatus values (cores/common/arduino/libraries/api/WiFi/WiFiType.h),
// repeated here so the host tests need not include the Arduino API.
// WiFiEvents.cpp static_asserts that they match.
#define LT_WIFI_RC_IDLE			0	// WL_IDLE_STATUS
#define LT_WIFI_RC_NO_SSID		1	// WL_NO_SSID_AVAIL
#define LT_WIFI_RC_CONNECTED	3	// WL_CONNECTED
#define LT_WIFI_RC_FAILED		4	// WL_CONNECT_FAILED
#define LT_WIFI_RC_LOST			5	// WL_CONNECTION_LOST
#define LT_WIFI_RC_DISCONNECTED 6	// WL_DISCONNECTED
#define LT_WIFI_RC_NO_SHIELD	255 // WL_NO_SHIELD

// Backoff between rejoin attempts, ramping so a vanished AP is not hammered.
#define LT_WIFI_RC_MIN_BACKOFF_MS 2000u
#define LT_WIFI_RC_MAX_BACKOFF_MS 30000u

// A join (or a renewal after a lost DHCP lease) that does not reach
// WL_CONNECTED within this window is abandoned. The chip itself reports a
// join within about 10 s; the rest is for the WPA2 handshake and DHCP.
#ifndef LT_WIFI_RC_JOIN_TIMEOUT_MS
#define LT_WIFI_RC_JOIN_TIMEOUT_MS 20000u
#endif

typedef enum {
	LT_WIFI_RC_ACTION_NONE = 0, // nothing to do this tick
	LT_WIFI_RC_ACTION_JOIN,		// send a join command now
	LT_WIFI_RC_ACTION_DEMOTE,	// supervised attempt timed out: disconnect, mark LOST (a later tick joins)
	LT_WIFI_RC_ACTION_FAIL,		// unsupervised attempt timed out: disconnect, mark CONNECT_FAILED
} lt_wifi_rc_action_t;

typedef struct {
	bool enabled;		   // auto-reconnect enabled (WiFi.setAutoReconnect)
	bool armed;			   // a connection reached WL_CONNECTED since the last begin()/disconnect()
	bool have_ssid;		   // a target SSID is stored
	int status;			   // current WiFiStatus
	uint32_t now_ms;	   // millis()
	uint32_t last_join_ms; // when the pending attempt started; 0 = none
	uint32_t backoff_ms;   // current backoff window
} lt_wifi_rc_input_t;

// Decides what to do this tick. No side effects.
lt_wifi_rc_action_t lt_wifi_rc_decide(const lt_wifi_rc_input_t *in);

// Backoff after a join was sent: 0 -> MIN, then doubling, capped at MAX.
uint32_t lt_wifi_rc_next_backoff(uint32_t cur);

#ifdef __cplusplus
}
#endif
