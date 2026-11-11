/* Copyright (c) Andrew Rankin 2026-06-12. */
#include "lt_wifi_sta.h"

static unsigned evaluate(lt_wifi_sta_t *s) {
	bool up = s->link_up && s->has_ip;
	if (up && s->status != LT_WIFI_RC_CONNECTED) {
		s->status = LT_WIFI_RC_CONNECTED;
		return LT_WIFI_STA_POST_GOT_IP;
	}
	if (!up && s->status == LT_WIFI_RC_CONNECTED && s->link_up) {
		// Still associated, address gone (lease expired or released).
		s->status = LT_WIFI_RC_IDLE;
		return LT_WIFI_STA_POST_LOST_IP;
	}
	return 0;
}

// Ends the attempt or link; returns whether the link was up.
static bool end_attempt(lt_wifi_sta_t *s, int status) {
	bool was_up = s->link_up;
	s->gen++;
	s->join_pending	 = false;
	s->assoc_pending = false;
	s->link_up		 = false;
	s->status		 = status;
	return was_up;
}

void lt_wifi_sta_init(lt_wifi_sta_t *s) {
	s->join_pending	 = false;
	s->assoc_pending = false;
	s->link_up		 = false;
	s->has_ip		 = false;
	s->status		 = LT_WIFI_RC_DISCONNECTED;
	s->gen			 = 0;
	s->owed			 = 0;
	s->owed_ms		 = 0;
}

void lt_wifi_sta_join_started(lt_wifi_sta_t *s) {
	s->gen++;
	s->join_pending	 = true;
	s->assoc_pending = false;
	s->link_up		 = false;
	s->status		 = LT_WIFI_RC_IDLE;
}

void lt_wifi_sta_join_refused(lt_wifi_sta_t *s, int status) {
	end_attempt(s, status); // nothing owed: the chip never took the join
}

unsigned lt_wifi_sta_connect_ind(lt_wifi_sta_t *s, lt_wifi_sta_join_t result, uint32_t now_ms) {
	if (s->owed != 0) {
		if ((now_ms - s->owed_ms) < LT_WIFI_STA_OWED_MS) {
			s->owed--; // the answer to an attempt the host already ended
			return 0;
		}
		s->owed = 0; // overdue: assume it was lost
	}
	if (!s->join_pending || result == LT_WIFI_STA_JOIN_ABORTED)
		return 0; // nothing to answer
	s->join_pending = false;
	if (result != LT_WIFI_STA_JOIN_OK) {
		s->status = result == LT_WIFI_STA_JOIN_NO_AP ? LT_WIFI_RC_NO_SSID : LT_WIFI_RC_FAILED;
		return LT_WIFI_STA_POST_DISCONNECTED;
	}
	s->assoc_pending = true;
	return LT_WIFI_STA_POST_LINK_UP;
}

unsigned lt_wifi_sta_link_ready(lt_wifi_sta_t *s) {
	if (!s->assoc_pending)
		return 0;
	s->assoc_pending = false;
	s->link_up		 = true;
	s->status		 = LT_WIFI_RC_IDLE;
	return LT_WIFI_STA_POST_CONNECTED | evaluate(s);
}

unsigned lt_wifi_sta_disconnect_ind(lt_wifi_sta_t *s) {
	if (!s->link_up && !s->assoc_pending)
		return 0; // the link was already taken down by the host
	s->assoc_pending = false;
	s->link_up		 = false;
	s->status		 = LT_WIFI_RC_LOST;
	return LT_WIFI_STA_POST_DISCONNECTED;
}

unsigned lt_wifi_sta_ip_changed(lt_wifi_sta_t *s, bool has_ip) {
	s->has_ip = has_ip;
	return evaluate(s);
}

unsigned lt_wifi_sta_local_down(lt_wifi_sta_t *s, int status, uint32_t now_ms) {
	if (s->join_pending) {
		if (s->owed < UINT8_MAX)
			s->owed++;
		s->owed_ms = now_ms;
	}
	return end_attempt(s, status) ? LT_WIFI_STA_POST_DISCONNECTED : 0;
}

unsigned lt_wifi_sta_attempt_failed(lt_wifi_sta_t *s, int status, uint32_t now_ms) {
	bool was_busy = s->link_up || s->assoc_pending || s->join_pending;
	lt_wifi_sta_local_down(s, status, now_ms);
	return was_busy ? LT_WIFI_STA_POST_DISCONNECTED : 0;
}

unsigned lt_wifi_sta_chip_down(lt_wifi_sta_t *s) {
	unsigned posts = lt_wifi_sta_attempt_failed(s, LT_WIFI_RC_NO_SHIELD, 0);
	s->owed		   = 0;
	return posts;
}
