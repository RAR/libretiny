/* Copyright (c) Andrew Rankin 2026-06-12. */
#pragma once

// Optional Wi-Fi status indicator on a board's RGB LED (LED_R/LED_G/LED_B
// from the board variant). Off by default, so the LEDs stay the
// application's: enable it at build time with -D LT_WIFI_STATUS_LED_DEFAULT=1
// or at run time with ltWifiStatusLedEnable(true). On boards without those
// macros every call is a no-op.
//
// Colours (active-high RGB):
//   RADIO_INIT  red      sl_wfx_init() running, at bring-up or a restart (stays red if the radio hangs)
//   RADIO_FAIL  magenta  radio, SDIO or netif fault, or the WF200 stopped (WL_NO_SHIELD)
//   IDLE        blue     radio and IP stack up, not connected
//   JOINING     yellow   join sent, waiting for association and an address
//   JOINED      green    WL_CONNECTED
//   JOIN_FAIL   white    join refused or timed out
typedef enum {
	LT_WIFI_LED_OFF = 0,
	LT_WIFI_LED_RADIO_INIT,
	LT_WIFI_LED_RADIO_FAIL,
	LT_WIFI_LED_IDLE,
	LT_WIFI_LED_JOINING,
	LT_WIFI_LED_JOINED,
	LT_WIFI_LED_JOIN_FAIL,
} LtWifiLedStage;

// Shows `stage` when enabled; a no-op otherwise. The first call after
// enabling configures the pins as outputs. Called from the application task
// and the WiFi event task; each channel is written with digitalWrite(), an
// atomic GPIO set/clear.
void ltWifiStatusLed(LtWifiLedStage stage);

// Enables or disables the indicator at run time. Disabling hands LED_R/G/B
// back to the application: no GPIO is touched until it is enabled again, and
// re-enabling reconfigures the pins as outputs on the next update.
void ltWifiStatusLedEnable(bool enabled);
