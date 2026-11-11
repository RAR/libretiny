/* Copyright (c) Andrew Rankin 2026-06-12. */
// Host unit tests for the station auto-reconnect decision.
#include "check.h"
#include "lt_wifi_reconnect.h"

// Armed and connected; each case changes what it tests.
static lt_wifi_rc_input_t base(void) {
	lt_wifi_rc_input_t in = {
		.enabled	  = true,
		.armed		  = true,
		.have_ssid	  = true,
		.status		  = LT_WIFI_RC_CONNECTED,
		.now_ms		  = 1000000,
		.last_join_ms = 0,
		.backoff_ms	  = 0,
	};
	return in;
}

int main(void) {
	// Never act unless enabled, armed and an SSID is stored.
	{
		lt_wifi_rc_input_t in = base();
		in.status			  = LT_WIFI_RC_LOST;
		in.enabled			  = false;
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_NONE);
	}
	{
		lt_wifi_rc_input_t in = base();
		in.status			  = LT_WIFI_RC_LOST;
		in.armed			  = false; // never connected: begin()'s caller decides
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_NONE);
	}
	{
		lt_wifi_rc_input_t in = base();
		in.status			  = LT_WIFI_RC_LOST;
		in.have_ssid		  = false;
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_NONE);
	}

	// A connected link is left alone.
	{
		lt_wifi_rc_input_t in = base();
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_NONE);
	}

	// Retryable states join; the first attempt is immediate.
	{
		int retryable[] = {LT_WIFI_RC_LOST, LT_WIFI_RC_FAILED, LT_WIFI_RC_DISCONNECTED, LT_WIFI_RC_NO_SSID};
		for (unsigned i = 0; i < sizeof(retryable) / sizeof(retryable[0]); i++) {
			lt_wifi_rc_input_t in = base();
			in.status			  = retryable[i];
			CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_JOIN);
		}
	}

	// The backoff window holds off a retry, then releases it.
	{
		lt_wifi_rc_input_t in = base();
		in.status			  = LT_WIFI_RC_LOST;
		in.backoff_ms		  = 4000;
		in.last_join_ms		  = in.now_ms - 3999;
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_NONE);
		in.last_join_ms = in.now_ms - 4000;
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_JOIN);
	}
	// The window survives millis() wrapping.
	{
		lt_wifi_rc_input_t in = base();
		in.status			  = LT_WIFI_RC_LOST;
		in.backoff_ms		  = 4000;
		in.now_ms			  = 1000;
		in.last_join_ms		  = 0xFFFFFFFFu - 1000; // 2001 ms ago
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_NONE);
	}

	// A pending attempt (IDLE) that does not complete in time is abandoned.
	{
		lt_wifi_rc_input_t in = base();
		in.status			  = LT_WIFI_RC_IDLE;
		in.last_join_ms		  = in.now_ms - (LT_WIFI_RC_JOIN_TIMEOUT_MS - 1);
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_NONE);
		in.last_join_ms = in.now_ms - LT_WIFI_RC_JOIN_TIMEOUT_MS;
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_DEMOTE);
	}
	// IDLE with no attempt being timed is left alone.
	{
		lt_wifi_rc_input_t in = base();
		in.status			  = LT_WIFI_RC_IDLE;
		in.last_join_ms		  = 0;
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_NONE);
	}

	// An application join (begin(): not armed) is timed too, and ends as a
	// failure rather than a retry.
	{
		lt_wifi_rc_input_t in = base();
		in.armed			  = false;
		in.status			  = LT_WIFI_RC_IDLE;
		in.last_join_ms		  = in.now_ms - (LT_WIFI_RC_JOIN_TIMEOUT_MS - 1);
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_NONE);
		in.last_join_ms = in.now_ms - LT_WIFI_RC_JOIN_TIMEOUT_MS;
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_FAIL);
		// ...and so is an armed one with auto-reconnect off (lost lease).
		in.armed   = true;
		in.enabled = false;
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_FAIL);
	}
	// The join timer survives millis() wrapping.
	{
		lt_wifi_rc_input_t in = base();
		in.armed			  = false;
		in.status			  = LT_WIFI_RC_IDLE;
		in.now_ms			  = 500;
		in.last_join_ms		  = 0xFFFFFFFFu - 1000; // 1501 ms ago
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_NONE);
	}
	// A failed application join is never retried.
	{
		lt_wifi_rc_input_t in = base();
		in.armed			  = false;
		in.status			  = LT_WIFI_RC_FAILED;
		in.last_join_ms		  = in.now_ms - 100000;
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_NONE);
	}
	// The radio being down (chip fault) is not a retryable state.
	{
		lt_wifi_rc_input_t in = base();
		in.status			  = LT_WIFI_RC_NO_SHIELD;
		CHECK(lt_wifi_rc_decide(&in) == LT_WIFI_RC_ACTION_NONE);
	}

	// Backoff ramp: 0 -> 2 s -> 4 s -> ... capped at 30 s.
	CHECK(lt_wifi_rc_next_backoff(0) == LT_WIFI_RC_MIN_BACKOFF_MS);
	CHECK(lt_wifi_rc_next_backoff(2000) == 4000);
	CHECK(lt_wifi_rc_next_backoff(4000) == 8000);
	CHECK(lt_wifi_rc_next_backoff(16000) == LT_WIFI_RC_MAX_BACKOFF_MS);
	CHECK(lt_wifi_rc_next_backoff(30000) == LT_WIFI_RC_MAX_BACKOFF_MS);

	return 0;
}
