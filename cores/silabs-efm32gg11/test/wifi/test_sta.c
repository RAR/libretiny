/* Copyright (c) Andrew Rankin 2026-06-12. */
// Host unit tests for the station state machine.
#include "check.h"
#include "lt_wifi_sta.h"

static uint32_t now = 100000;

// CONNECT_IND success as the WiFi library handles it: link up, then ready.
static unsigned join_ok(lt_wifi_sta_t *s) {
	unsigned posts = lt_wifi_sta_connect_ind(s, LT_WIFI_STA_JOIN_OK, now);
	if (posts != LT_WIFI_STA_POST_LINK_UP)
		return posts == 0 ? 0 : 0x80000000u | posts;
	return lt_wifi_sta_link_ready(s);
}

int main(void) {
	lt_wifi_sta_t s;
	uint32_t g;

	// DHCP: association alone is not a connection; the lease completes it.
	lt_wifi_sta_init(&s);
	CHECK(s.status == LT_WIFI_RC_DISCONNECTED);
	lt_wifi_sta_join_started(&s);
	CHECK(s.status == LT_WIFI_RC_IDLE);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_OK, now) == LT_WIFI_STA_POST_LINK_UP);
	CHECK(s.assoc_pending && !s.link_up && !s.join_pending && s.status == LT_WIFI_RC_IDLE);
	CHECK(lt_wifi_sta_link_ready(&s) == LT_WIFI_STA_POST_CONNECTED);
	CHECK(s.status == LT_WIFI_RC_IDLE && s.link_up);
	CHECK(lt_wifi_sta_ip_changed(&s, true) == LT_WIFI_STA_POST_GOT_IP);
	CHECK(s.status == LT_WIFI_RC_CONNECTED);
	CHECK(lt_wifi_sta_ip_changed(&s, true) == 0); // no repeated GOT_IP
	// Lease lost while associated.
	CHECK(lt_wifi_sta_ip_changed(&s, false) == LT_WIFI_STA_POST_LOST_IP);
	CHECK(s.status == LT_WIFI_RC_IDLE);
	CHECK(lt_wifi_sta_ip_changed(&s, true) == LT_WIFI_STA_POST_GOT_IP);
	// AP lost.
	CHECK(lt_wifi_sta_disconnect_ind(&s) == LT_WIFI_STA_POST_DISCONNECTED);
	CHECK(s.status == LT_WIFI_RC_LOST && !s.link_up);

	// Static address: configured before the join, so it must not report a
	// connection without association...
	lt_wifi_sta_init(&s);
	CHECK(lt_wifi_sta_ip_changed(&s, true) == 0);
	CHECK(s.status == LT_WIFI_RC_DISCONNECTED);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_ip_changed(&s, true) == 0);
	CHECK(s.status == LT_WIFI_RC_IDLE);
	// ...and connects (with GOT_IP) as soon as the association completes.
	CHECK(join_ok(&s) == (LT_WIFI_STA_POST_CONNECTED | LT_WIFI_STA_POST_GOT_IP));
	CHECK(s.status == LT_WIFI_RC_CONNECTED);
	// A supervisor rejoin with the static address promotes again.
	CHECK(lt_wifi_sta_disconnect_ind(&s) == LT_WIFI_STA_POST_DISCONNECTED);
	lt_wifi_sta_join_started(&s);
	CHECK(join_ok(&s) == (LT_WIFI_STA_POST_CONNECTED | LT_WIFI_STA_POST_GOT_IP));
	CHECK(s.status == LT_WIFI_RC_CONNECTED);

	// Re-begin while connected: the DISCONNECT_IND of the old link arrives
	// after the new join was sent and must not fail the new attempt.
	CHECK(lt_wifi_sta_local_down(&s, LT_WIFI_RC_DISCONNECTED, now) == LT_WIFI_STA_POST_DISCONNECTED);
	CHECK(s.owed == 0); // that join had been answered
	lt_wifi_sta_ip_changed(&s, false);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_disconnect_ind(&s) == 0);
	CHECK(s.status == LT_WIFI_RC_IDLE && s.join_pending);
	CHECK(join_ok(&s) == LT_WIFI_STA_POST_CONNECTED);
	CHECK(lt_wifi_sta_ip_changed(&s, true) == LT_WIFI_STA_POST_GOT_IP);

	// Join refused by the chip.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_FAILED, now) == LT_WIFI_STA_POST_DISCONNECTED);
	CHECK(s.status == LT_WIFI_RC_FAILED && !s.join_pending);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_NO_AP, now) == LT_WIFI_STA_POST_DISCONNECTED);
	CHECK(s.status == LT_WIFI_RC_NO_SSID);

	// Only one CONNECT_IND counts per join.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	CHECK(join_ok(&s) == LT_WIFI_STA_POST_CONNECTED);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_FAILED, now) == 0);
	CHECK(s.link_up && s.status == LT_WIFI_RC_IDLE);

	// Host-side teardown of a link that is not up posts nothing.
	lt_wifi_sta_init(&s);
	CHECK(lt_wifi_sta_local_down(&s, LT_WIFI_RC_DISCONNECTED, now) == 0);

	// Attempt generation: a supervisor decision taken at one generation is
	// void once the application started or ended an attempt.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	join_ok(&s);
	lt_wifi_sta_disconnect_ind(&s); // LOST: the supervisor decides to act
	uint32_t decided = s.gen;
	lt_wifi_sta_local_down(&s, LT_WIFI_RC_DISCONNECTED, now); // begin(): leave...
	lt_wifi_sta_join_started(&s);							  // ...and join
	CHECK(s.gen != decided);
	// Indications do not change the generation.
	g = s.gen;
	join_ok(&s);
	lt_wifi_sta_ip_changed(&s, true);
	lt_wifi_sta_disconnect_ind(&s);
	CHECK(s.gen == g);

	// ---- Cancelled joins: their CONNECT_IND is owed and never credited to
	// the next join, whatever its status.

	// The chip answers the cancelled join with ABORTED: that is the owed
	// indication, and the next join's own result still counts.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_local_down(&s, LT_WIFI_RC_DISCONNECTED, now) == 0); // begin() again
	CHECK(s.owed == 1);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_ABORTED, now + 50) == 0);
	CHECK(s.owed == 0 && s.join_pending && s.status == LT_WIFI_RC_IDLE);
	CHECK(join_ok(&s) == LT_WIFI_STA_POST_CONNECTED);
	CHECK(lt_wifi_sta_ip_changed(&s, true) == LT_WIFI_STA_POST_GOT_IP);

	// The first join completed (success) just as the host cancelled it: its
	// late success is dropped; the second join's failure is the result.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	lt_wifi_sta_local_down(&s, LT_WIFI_RC_DISCONNECTED, now);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_OK, now + 10) == 0);
	CHECK(s.join_pending && !s.assoc_pending && !s.link_up);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_FAILED, now + 3000) == LT_WIFI_STA_POST_DISCONNECTED);
	CHECK(s.status == LT_WIFI_RC_FAILED);

	// ...and a late failure is dropped; the second join's success counts.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	lt_wifi_sta_local_down(&s, LT_WIFI_RC_DISCONNECTED, now);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_FAILED, now + 10) == 0);
	CHECK(s.join_pending && s.status == LT_WIFI_RC_IDLE);
	CHECK(join_ok(&s) == LT_WIFI_STA_POST_CONNECTED);

	// disconnect() during a join: the owed ABORTED changes nothing, and a
	// stray success after it is ignored too (no join pending).
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_local_down(&s, LT_WIFI_RC_DISCONNECTED, now) == 0);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_ABORTED, now) == 0);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_OK, now) == 0);
	CHECK(s.status == LT_WIFI_RC_DISCONNECTED && !s.link_up && s.owed == 0);

	// Two joins cancelled in a row: two indications owed, in order.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	lt_wifi_sta_local_down(&s, LT_WIFI_RC_DISCONNECTED, now);
	lt_wifi_sta_join_started(&s);
	lt_wifi_sta_local_down(&s, LT_WIFI_RC_DISCONNECTED, now + 100);
	CHECK(s.owed == 2);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_ABORTED, now + 200) == 0);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_ABORTED, now + 300) == 0);
	CHECK(join_ok(&s) == LT_WIFI_STA_POST_CONNECTED);

	// An owed indication that never comes is forgotten after
	// LT_WIFI_STA_OWED_MS, so the next join's result is not lost.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	lt_wifi_sta_local_down(&s, LT_WIFI_RC_DISCONNECTED, now);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_OK, now + LT_WIFI_STA_OWED_MS) == LT_WIFI_STA_POST_LINK_UP);
	CHECK(s.owed == 0 && lt_wifi_sta_link_ready(&s) == LT_WIFI_STA_POST_CONNECTED);
	// The window survives millis() wrapping.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	lt_wifi_sta_local_down(&s, LT_WIFI_RC_DISCONNECTED, 0xFFFFFF00u);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_OK, 0x100u) == 0); // 512 ms later: still owed

	// A join the chip refused is owed nothing.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	lt_wifi_sta_join_refused(&s, LT_WIFI_RC_FAILED);
	CHECK(s.owed == 0 && s.status == LT_WIFI_RC_FAILED && !s.join_pending);
	lt_wifi_sta_join_started(&s);
	CHECK(join_ok(&s) == LT_WIFI_STA_POST_CONNECTED);

	// The host ends the attempt between the success and the link coming up
	// (begin() on another task): link_ready refuses, nothing is owed (the
	// indication arrived), and the chip's DISCONNECT_IND is ignored.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_OK, now) == LT_WIFI_STA_POST_LINK_UP);
	CHECK(lt_wifi_sta_local_down(&s, LT_WIFI_RC_DISCONNECTED, now) == 0);
	CHECK(s.owed == 0);
	CHECK(lt_wifi_sta_link_ready(&s) == 0);
	CHECK(lt_wifi_sta_disconnect_ind(&s) == 0);
	CHECK(!s.link_up && s.status == LT_WIFI_RC_DISCONNECTED);
	// The AP drops the station in that same window: reported as lost.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_OK, now);
	CHECK(lt_wifi_sta_disconnect_ind(&s) == LT_WIFI_STA_POST_DISCONNECTED);
	CHECK(lt_wifi_sta_link_ready(&s) == 0 && s.status == LT_WIFI_RC_LOST && !s.link_up);

	// ---- Failures the host detects.

	// Join timed out before any CONNECT_IND: the failure posts a disconnect,
	// so an application waiting on events sees the outcome; the chip's late
	// answer (to the host's cancel) is owed and dropped.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	g = s.gen;
	CHECK(lt_wifi_sta_attempt_failed(&s, LT_WIFI_RC_FAILED, now) == LT_WIFI_STA_POST_DISCONNECTED);
	CHECK(s.status == LT_WIFI_RC_FAILED && !s.join_pending && s.gen != g && s.owed == 1);
	CHECK(lt_wifi_sta_connect_ind(&s, LT_WIFI_STA_JOIN_OK, now + 100) == 0);
	CHECK(s.owed == 0 && s.status == LT_WIFI_RC_FAILED);

	// Associated but no lease in time: same, nothing owed.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	CHECK(join_ok(&s) == LT_WIFI_STA_POST_CONNECTED);
	CHECK(s.status == LT_WIFI_RC_IDLE);
	CHECK(lt_wifi_sta_attempt_failed(&s, LT_WIFI_RC_FAILED, now) == LT_WIFI_STA_POST_DISCONNECTED);
	CHECK(s.status == LT_WIFI_RC_FAILED && !s.link_up && s.owed == 0);
	CHECK(lt_wifi_sta_disconnect_ind(&s) == 0); // the chip's DISCONNECT_IND for it

	// A failure with nothing in progress posts nothing.
	lt_wifi_sta_init(&s);
	CHECK(lt_wifi_sta_attempt_failed(&s, LT_WIFI_RC_NO_SHIELD, now) == 0);
	CHECK(s.status == LT_WIFI_RC_NO_SHIELD);

	// Chip fault while connected: down with NO_SHIELD.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	join_ok(&s);
	lt_wifi_sta_ip_changed(&s, true);
	CHECK(lt_wifi_sta_chip_down(&s) == LT_WIFI_STA_POST_DISCONNECTED);
	CHECK(lt_wifi_sta_ip_changed(&s, false) == 0);
	CHECK(s.status == LT_WIFI_RC_NO_SHIELD);
	// After the restart the station is marked lost (no second event) and
	// rejoined like after any link loss.
	CHECK(lt_wifi_sta_local_down(&s, LT_WIFI_RC_LOST, now) == 0);
	lt_wifi_sta_join_started(&s);
	CHECK(join_ok(&s) == LT_WIFI_STA_POST_CONNECTED);
	// Chip fault during a join: posts a disconnect, and the dead chip owes
	// nothing, so the first join after the restart counts.
	lt_wifi_sta_init(&s);
	lt_wifi_sta_join_started(&s);
	CHECK(lt_wifi_sta_chip_down(&s) == LT_WIFI_STA_POST_DISCONNECTED);
	CHECK(s.owed == 0);
	lt_wifi_sta_local_down(&s, LT_WIFI_RC_DISCONNECTED, now);
	lt_wifi_sta_join_started(&s);
	CHECK(join_ok(&s) == LT_WIFI_STA_POST_CONNECTED);

	return 0;
}
