/* Copyright (c) Andrew Rankin 2026-06-12. */
#include "lt_wifi_reconnect.h"

lt_wifi_rc_action_t lt_wifi_rc_decide(const lt_wifi_rc_input_t *in) {
	// Only a connection that was established (armed) and is still wanted is
	// supervised. A join that never succeeded is reported to the application
	// (status() and STA_DISCONNECTED) and never retried here.
	bool supervised = in->enabled && in->armed && in->have_ssid;

	if (in->status == LT_WIFI_RC_IDLE) {
		// A join, or a renewal after the lease was lost, is pending. If it does
		// not complete within the window, end it. last_join_ms == 0 means no
		// attempt is being timed.
		if (in->last_join_ms == 0 || (in->now_ms - in->last_join_ms) < LT_WIFI_RC_JOIN_TIMEOUT_MS)
			return LT_WIFI_RC_ACTION_NONE;
		return supervised ? LT_WIFI_RC_ACTION_DEMOTE : LT_WIFI_RC_ACTION_FAIL;
	}
	if (!supervised)
		return LT_WIFI_RC_ACTION_NONE;

	switch (in->status) {
		case LT_WIFI_RC_NO_SSID:
		case LT_WIFI_RC_LOST:
		case LT_WIFI_RC_FAILED:
		case LT_WIFI_RC_DISCONNECTED:
			// Retryable. The first attempt (last_join_ms == 0) is immediate;
			// later ones wait out the backoff window.
			if (in->last_join_ms != 0 && (in->now_ms - in->last_join_ms) < in->backoff_ms)
				return LT_WIFI_RC_ACTION_NONE;
			return LT_WIFI_RC_ACTION_JOIN;

		default: // CONNECTED, or NO_SHIELD (the radio is down)
			return LT_WIFI_RC_ACTION_NONE;
	}
}

uint32_t lt_wifi_rc_next_backoff(uint32_t cur) {
	if (cur == 0)
		return LT_WIFI_RC_MIN_BACKOFF_MS;
	uint32_t next = cur * 2;
	return next < LT_WIFI_RC_MAX_BACKOFF_MS ? next : LT_WIFI_RC_MAX_BACKOFF_MS;
}
