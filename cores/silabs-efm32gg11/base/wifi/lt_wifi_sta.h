/* Copyright (c) Andrew Rankin 2026-06-12. */
// Pure station state machine: derives the published WiFiStatus from driver
// indications and the netif address, and says which Arduino events a
// transition must post. No hardware, FreeRTOS or lwIP dependencies, so it is
// unit-tested on the host (cores/silabs-efm32gg11/test/wifi). The WiFi
// library calls it with its station lock held.
//
// WL_CONNECTED means associated AND holding a non-zero IPv4 address, whether
// the address came from DHCP or from config(). Association alone, and a join
// in progress, are WL_IDLE_STATUS.
//
// Indications are matched to the attempt they belong to:
//   - the chip answers every accepted join with exactly one CONNECT_IND: its
//     outcome, or CONNECTION_ABORTED when the host cancelled it with a
//     disconnect request first. When the host ends an attempt whose
//     CONNECT_IND has not arrived (begin() leaving it for a new one,
//     disconnect(), the join timer), that indication is owed, and the next
//     CONNECT_IND is taken to be it and dropped, whatever its status: it can
//     arrive after the next join was sent and must not be credited to that
//     one. The chip processes joins in order (a join is refused until the
//     station interface is idle), so the owed one comes first. An owed
//     indication that has not come within LT_WIFI_STA_OWED_MS is forgotten,
//     so a lost one cannot swallow a later join's result for long;
//   - a DISCONNECT_IND counts only while associated. The firmware also sends
//     one for a host-requested disconnect, and it can arrive after the next
//     join was sent; by then the link is already marked down, so it is
//     ignored instead of failing the new attempt.
// `gen` changes whenever the host starts or ends an attempt. A task that
// decides on a snapshot (the event task's join timer and auto-reconnect
// supervisor) acts only if `gen` is unchanged when it takes the lock to act,
// so it cannot undo an attempt the application started in between.
#pragma once

#include "lt_wifi_reconnect.h"

#ifdef __cplusplus
extern "C" {
#endif

// Events a transition asks the caller to post.
#define LT_WIFI_STA_POST_CONNECTED	  0x01u // ARDUINO_EVENT_WIFI_STA_CONNECTED
#define LT_WIFI_STA_POST_DISCONNECTED 0x02u // ARDUINO_EVENT_WIFI_STA_DISCONNECTED
#define LT_WIFI_STA_POST_GOT_IP		  0x04u // ARDUINO_EVENT_WIFI_STA_GOT_IP
#define LT_WIFI_STA_POST_LOST_IP	  0x08u // ARDUINO_EVENT_WIFI_STA_LOST_IP
// Not an event: the join succeeded; bring the netif link up (outside the
// lock), then call lt_wifi_sta_link_ready().
#define LT_WIFI_STA_POST_LINK_UP	  0x10u

// How long an owed CONNECT_IND is waited for (see above).
#ifndef LT_WIFI_STA_OWED_MS
#define LT_WIFI_STA_OWED_MS 5000u
#endif

// Outcome of a join, from the CONNECT_IND status.
typedef enum {
	LT_WIFI_STA_JOIN_OK = 0,
	LT_WIFI_STA_JOIN_FAILED,  // refused, wrong password, timed out on the chip
	LT_WIFI_STA_JOIN_NO_AP,	  // WFM_STATUS_NO_MATCHING_AP
	LT_WIFI_STA_JOIN_ABORTED, // WFM_STATUS_CONNECTION_ABORTED (host cancelled it)
} lt_wifi_sta_join_t;

typedef struct {
	bool join_pending;	// a join was sent and its CONNECT_IND has not arrived
	bool assoc_pending; // CONNECT_IND success seen; netif link being brought up
	bool link_up;		// associated: link ready, no DISCONNECT_IND since
	bool has_ip;		// the STA netif holds a non-zero IPv4 address
	int status;			// published WiFiStatus (LT_WIFI_RC_*)
	uint32_t gen;		// attempt generation, see above
	uint8_t owed;		// CONNECT_INDs of ended attempts still to come
	uint32_t owed_ms;	// when the last one became owed
} lt_wifi_sta_t;

void lt_wifi_sta_init(lt_wifi_sta_t *s);
// A join command is about to be sent.
void lt_wifi_sta_join_started(lt_wifi_sta_t *s);
// The join command was refused, so no CONNECT_IND will come for it.
void lt_wifi_sta_join_refused(lt_wifi_sta_t *s, int status);
// CONNECT_IND at now_ms. A success returns LT_WIFI_STA_POST_LINK_UP and
// nothing else; the other events follow from lt_wifi_sta_link_ready().
unsigned lt_wifi_sta_connect_ind(lt_wifi_sta_t *s, lt_wifi_sta_join_t result, uint32_t now_ms);
// The netif link is up after LT_WIFI_STA_POST_LINK_UP. Returns 0 if the host
// ended the attempt meanwhile (the caller takes the link down again).
unsigned lt_wifi_sta_link_ready(lt_wifi_sta_t *s);
// DISCONNECT_IND.
unsigned lt_wifi_sta_disconnect_ind(lt_wifi_sta_t *s);
// The netif address may have changed.
unsigned lt_wifi_sta_ip_changed(lt_wifi_sta_t *s, bool has_ip);
// The host ended the attempt or the link on request (disconnect(), begin()
// leaving the current network, STA disable) at now_ms; status is the
// WiFiStatus to publish. Posts STA_DISCONNECTED only if the link was up.
unsigned lt_wifi_sta_local_down(lt_wifi_sta_t *s, int status, uint32_t now_ms);
// The host gave up on the attempt or link because it failed (join timed
// out, no address in time). Like lt_wifi_sta_local_down(), but also posts
// STA_DISCONNECTED for a join that was still pending, so an application
// waiting for the outcome of begin() sees one.
unsigned lt_wifi_sta_attempt_failed(lt_wifi_sta_t *s, int status, uint32_t now_ms);
// The chip stopped (firmware exception): as lt_wifi_sta_attempt_failed()
// with WL_NO_SHIELD, and nothing is owed any more.
unsigned lt_wifi_sta_chip_down(lt_wifi_sta_t *s);

#ifdef __cplusplus
}
#endif
