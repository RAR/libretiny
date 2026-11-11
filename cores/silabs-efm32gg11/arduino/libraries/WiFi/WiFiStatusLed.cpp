/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "WiFiStatusLed.h"

#include <Arduino.h>

#if defined(LED_R) && defined(LED_G) && defined(LED_B)

#ifndef LT_WIFI_STATUS_LED_DEFAULT
#define LT_WIFI_STATUS_LED_DEFAULT 0
#endif

// Single-byte flags: atomic on Cortex-M4; volatile so each call re-reads them.
static volatile bool s_enabled = LT_WIFI_STATUS_LED_DEFAULT;
static volatile bool s_inited  = false;

void ltWifiStatusLedEnable(bool enabled) {
	s_enabled = enabled;
	if (!enabled)
		s_inited = false; // the application may change the pin modes meanwhile
}

void ltWifiStatusLed(LtWifiLedStage stage) {
	if (!s_enabled)
		return;

	if (!s_inited) {
		pinMode(LED_R, OUTPUT);
		pinMode(LED_G, OUTPUT);
		pinMode(LED_B, OUTPUT);
		s_inited = true;
	}

	bool r = false, g = false, b = false;
	switch (stage) {
		case LT_WIFI_LED_RADIO_INIT:
			r = true;
			break;
		case LT_WIFI_LED_RADIO_FAIL:
			r = b = true;
			break;
		case LT_WIFI_LED_IDLE:
			b = true;
			break;
		case LT_WIFI_LED_JOINING:
			r = g = true;
			break;
		case LT_WIFI_LED_JOINED:
			g = true;
			break;
		case LT_WIFI_LED_JOIN_FAIL:
			r = g = b = true;
			break;
		case LT_WIFI_LED_OFF:
		default:
			break;
	}

	digitalWrite(LED_R, r ? HIGH : LOW);
	digitalWrite(LED_G, g ? HIGH : LOW);
	digitalWrite(LED_B, b ? HIGH : LOW);
}

#else // no RGB LED on this board

void ltWifiStatusLed(LtWifiLedStage stage) {
	(void)stage;
}

void ltWifiStatusLedEnable(bool enabled) {
	(void)enabled;
}

#endif
