/* Copyright (c) Andrew Rankin 2026-06-12. */
#include "lt_wifi_recovery.h"

#include "lt_wifi_reconnect.h"

void lt_wifi_recovery_init(lt_wifi_recovery_t *r) {
	r->state	= LT_WIFI_RECOVERY_IDLE;
	r->attempts = 0;
	r->due_ms	= 0;
	r->up_ms	= 0;
}

uint32_t lt_wifi_recovery_delay(uint8_t attempts) {
	uint32_t delay = LT_WIFI_RECOVERY_DELAY_MS;
	for (uint8_t i = 0; i < attempts; i++) {
		if (delay > LT_WIFI_RECOVERY_MAX_DELAY_MS / 4)
			return LT_WIFI_RECOVERY_MAX_DELAY_MS; // also keeps delay * 4 from overflowing
		delay *= 4;
	}
	return delay < LT_WIFI_RECOVERY_MAX_DELAY_MS ? delay : LT_WIFI_RECOVERY_MAX_DELAY_MS;
}

static bool schedule(lt_wifi_recovery_t *r, uint32_t now_ms) {
	if (r->attempts >= LT_WIFI_RECOVERY_MAX_ATTEMPTS) {
		r->state = LT_WIFI_RECOVERY_GAVE_UP;
		return false;
	}
	r->state  = LT_WIFI_RECOVERY_PENDING;
	r->due_ms = now_ms + lt_wifi_recovery_delay(r->attempts);
	return true;
}

bool lt_wifi_recovery_fault(lt_wifi_recovery_t *r, uint32_t now_ms) {
	if (r->state == LT_WIFI_RECOVERY_PENDING)
		return true; // already scheduled
	if (r->state == LT_WIFI_RECOVERY_GAVE_UP)
		return false;
	// The chip ran long enough since the last restart: start counting afresh.
	if (r->attempts != 0 && (now_ms - r->up_ms) >= LT_WIFI_RECOVERY_STABLE_MS)
		r->attempts = 0;
	return schedule(r, now_ms);
}

bool lt_wifi_recovery_due(const lt_wifi_recovery_t *r, uint32_t now_ms) {
	return r->state == LT_WIFI_RECOVERY_PENDING && (int32_t)(now_ms - r->due_ms) >= 0;
}

bool lt_wifi_recovery_done(lt_wifi_recovery_t *r, uint32_t now_ms, bool ok) {
	if (r->attempts < UINT8_MAX)
		r->attempts++;
	if (ok) {
		r->state = LT_WIFI_RECOVERY_IDLE;
		r->up_ms = now_ms;
		return true;
	}
	return schedule(r, now_ms);
}

lt_wifi_restore_t lt_wifi_recovery_plan(bool sta_armed, bool auto_reconnect, bool ap_configured) {
	lt_wifi_restore_t plan = {
		.sta_status = sta_armed ? LT_WIFI_RC_LOST : LT_WIFI_RC_DISCONNECTED,
		.rejoin_sta = sta_armed && auto_reconnect,
		.restart_ap = ap_configured,
	};
	return plan;
}
