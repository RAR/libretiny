/* Copyright (c) Andrew Rankin 2026-06-12. */
// Host unit tests for the WF200 restart policy.
#include "check.h"
#include "lt_wifi_reconnect.h"
#include "lt_wifi_recovery.h"

int main(void) {
	lt_wifi_recovery_t r;

	// Delays: 2 s, 8 s, 32 s, then capped.
	CHECK(lt_wifi_recovery_delay(0) == LT_WIFI_RECOVERY_DELAY_MS);
	CHECK(lt_wifi_recovery_delay(1) == LT_WIFI_RECOVERY_DELAY_MS * 4);
	CHECK(lt_wifi_recovery_delay(2) == LT_WIFI_RECOVERY_DELAY_MS * 16);
	CHECK(lt_wifi_recovery_delay(3) == LT_WIFI_RECOVERY_MAX_DELAY_MS);
	CHECK(lt_wifi_recovery_delay(200) == LT_WIFI_RECOVERY_MAX_DELAY_MS);

	// A fault schedules a restart after the first delay, not at once.
	lt_wifi_recovery_init(&r);
	uint32_t t = 100000;
	CHECK(lt_wifi_recovery_fault(&r, t));
	CHECK(r.state == LT_WIFI_RECOVERY_PENDING);
	CHECK(!lt_wifi_recovery_due(&r, t));
	CHECK(!lt_wifi_recovery_due(&r, t + LT_WIFI_RECOVERY_DELAY_MS - 1));
	CHECK(lt_wifi_recovery_due(&r, t + LT_WIFI_RECOVERY_DELAY_MS));
	// A repeated fault report while pending changes nothing.
	CHECK(lt_wifi_recovery_fault(&r, t + 1000));
	CHECK(r.due_ms == t + LT_WIFI_RECOVERY_DELAY_MS);
	// It works: back to idle, nothing due.
	t += LT_WIFI_RECOVERY_DELAY_MS;
	CHECK(lt_wifi_recovery_done(&r, t, true));
	CHECK(r.state == LT_WIFI_RECOVERY_IDLE && r.attempts == 1);
	CHECK(!lt_wifi_recovery_due(&r, t + 1000000));

	// Restarts that keep failing: bounded, with growing delays, then give up.
	lt_wifi_recovery_init(&r);
	t = 5000;
	CHECK(lt_wifi_recovery_fault(&r, t));
	for (unsigned n = 1; n <= LT_WIFI_RECOVERY_MAX_ATTEMPTS; n++) {
		t = r.due_ms;
		CHECK(lt_wifi_recovery_due(&r, t));
		bool more = lt_wifi_recovery_done(&r, t, false);
		if (n < LT_WIFI_RECOVERY_MAX_ATTEMPTS) {
			CHECK(more && r.state == LT_WIFI_RECOVERY_PENDING);
			CHECK(r.due_ms == t + lt_wifi_recovery_delay((uint8_t)n));
		} else {
			CHECK(!more && r.state == LT_WIFI_RECOVERY_GAVE_UP);
		}
	}
	// Given up: no further restarts, whatever happens.
	CHECK(!lt_wifi_recovery_due(&r, t + 10000000));
	CHECK(!lt_wifi_recovery_fault(&r, t + 10000000));
	CHECK(r.state == LT_WIFI_RECOVERY_GAVE_UP);

	// A chip that faults again soon after each successful restart uses up
	// the attempts too: no endless reset loop.
	lt_wifi_recovery_init(&r);
	t				  = 1000;
	unsigned restarts = 0;
	while (lt_wifi_recovery_fault(&r, t)) {
		t = r.due_ms;
		CHECK(lt_wifi_recovery_done(&r, t, true));
		restarts++;
		t += 60000; // runs a minute, then faults again
		CHECK(restarts <= LT_WIFI_RECOVERY_MAX_ATTEMPTS);
	}
	CHECK(restarts == LT_WIFI_RECOVERY_MAX_ATTEMPTS);
	CHECK(r.state == LT_WIFI_RECOVERY_GAVE_UP);

	// A chip that ran for the stable window since its last restart starts
	// counting afresh, with the shortest delay again.
	lt_wifi_recovery_init(&r);
	t = 1000;
	CHECK(lt_wifi_recovery_fault(&r, t));
	t = r.due_ms;
	CHECK(lt_wifi_recovery_done(&r, t, true));
	CHECK(lt_wifi_recovery_fault(&r, t + 1000)); // soon: second attempt
	CHECK(r.attempts == 1 && r.due_ms == t + 1000 + lt_wifi_recovery_delay(1));
	t = r.due_ms;
	CHECK(lt_wifi_recovery_done(&r, t, true));
	CHECK(r.attempts == 2);
	t += LT_WIFI_RECOVERY_STABLE_MS;
	CHECK(lt_wifi_recovery_fault(&r, t));
	CHECK(r.attempts == 0 && r.due_ms == t + LT_WIFI_RECOVERY_DELAY_MS);

	// The schedule survives millis() wrapping.
	lt_wifi_recovery_init(&r);
	t = 0xFFFFFFFFu - 500;
	CHECK(lt_wifi_recovery_fault(&r, t));
	CHECK(!lt_wifi_recovery_due(&r, t + 1000)); // wrapped, 1 s in
	CHECK(lt_wifi_recovery_due(&r, t + LT_WIFI_RECOVERY_DELAY_MS));

	// What is restored.
	lt_wifi_restore_t plan = lt_wifi_recovery_plan(true, true, true);
	CHECK(plan.sta_status == LT_WIFI_RC_LOST && plan.rejoin_sta && plan.restart_ap);
	// Auto-reconnect off: lost, but the application rejoins it.
	plan = lt_wifi_recovery_plan(true, false, true);
	CHECK(plan.sta_status == LT_WIFI_RC_LOST && !plan.rejoin_sta && plan.restart_ap);
	plan = lt_wifi_recovery_plan(false, true, false);
	CHECK(plan.sta_status == LT_WIFI_RC_DISCONNECTED && !plan.rejoin_sta && !plan.restart_ap);
	// Should the immediate rejoin be refused, the supervisor retries a
	// restored station at once (LOST, no attempt being timed), and leaves one
	// that had never connected alone.
	{
		lt_wifi_rc_input_t in = {
			.enabled	  = true,
			.armed		  = true,
			.have_ssid	  = true,
			.status		  = lt_wifi_recovery_plan(true, true, false).sta_status,
			.now_ms		  = 1234,
			.last_join_ms = 0,
			.backoff_ms	  = 0,
		};
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_JOIN);
		in.armed  = false;
		in.status = lt_wifi_recovery_plan(false, true, false).sta_status;
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_NONE);
	}

	return 0;
}
