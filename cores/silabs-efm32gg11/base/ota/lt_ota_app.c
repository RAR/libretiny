/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "lt_ota_app.h"

#include <string.h>

// Newest record, or a blank one (all banks EMPTY, booting the running bank).
static void load_or_blank(const lt_ota_app_t *app, lt_ota_meta_t *m) {
	if (lt_ota_meta_load(app->ops, m))
		return;
	memset(m, 0, sizeof(*m));
	m->boot_bank = app->running;
}

static bool state_offered(const lt_ota_meta_t *m, uint8_t bank) {
	return m->state[bank] >= LT_OTA_BANK_VALID && m->state[bank] <= LT_OTA_BANK_CONFIRMED;
}

static void set_state(lt_ota_meta_t *m, uint8_t bank, uint8_t state, uint32_t crc) {
	m->state[bank]	   = state;
	m->image_crc[bank] = state == LT_OTA_BANK_EMPTY ? 0 : crc;
}

// The bank holds the image its recorded state belongs to (lt_ota_boot.h: usable).
static bool usable(const lt_ota_app_t *app, const lt_ota_meta_t *m, uint8_t bank) {
	uint32_t crc;
	return state_offered(m, bank) && lt_ota_image_check(app->ops, bank, &crc) == LT_OTA_IMAGE_OK &&
		   crc == m->image_crc[bank];
}

// Record the running image (see lt_ota_app.h). Returns false if it does not
// verify, in which case the running bank is recorded EMPTY.
static bool record_running(const lt_ota_app_t *app, lt_ota_meta_t *m) {
	uint8_t bank = app->running;
	uint32_t crc;
	if (lt_ota_image_check(app->ops, bank, &crc) != LT_OTA_IMAGE_OK) {
		set_state(m, bank, LT_OTA_BANK_EMPTY, 0);
		return false;
	}
	// Running is what VALID means. An existing state is kept only if it was
	// recorded for this very image (e.g. a TRIAL the bootloader is booting).
	if (!state_offered(m, bank) || m->image_crc[bank] != crc)
		set_state(m, bank, LT_OTA_BANK_VALID, crc);
	return true;
}

uint8_t lt_ota_app_stored(const lt_ota_app_t *app) {
	lt_ota_meta_t m;
	if (lt_ota_meta_load(app->ops, &m) && m.boot_bank < LT_OTA_BANK_COUNT)
		return m.boot_bank;
	// No usable record: the bootloader boots the first bank that verifies,
	// which is the running one unless the other bank was flashed since.
	return app->running;
}

bool lt_ota_app_is_valid(const lt_ota_app_t *app, uint8_t bank) {
	if (bank >= LT_OTA_BANK_COUNT)
		return false;
	if (bank == app->running)
		return true;
	lt_ota_meta_t m;
	return lt_ota_meta_load(app->ops, &m) && usable(app, &m, bank);
}

bool lt_ota_app_switch(const lt_ota_app_t *app, bool revert) {
	lt_ota_meta_t m;
	load_or_blank(app, &m);
	uint8_t stored = m.boot_bank < LT_OTA_BANK_COUNT ? m.boot_bank : app->running;
	uint8_t other  = app->running ^ 1u;

	if (revert) {
		if (stored == app->running)
			return true;
		// If the running image cannot be recorded, the bootloader would pass
		// over the running bank and boot the cancelled update instead.
		if (!record_running(app, &m))
			set_state(&m, other, LT_OTA_BANK_EMPTY, 0);
		m.boot_bank = app->running;
		// An unconfirmed running TRIAL image stays on trial. Give it its
		// attempts back: to the bootloader, TRIAL with none left means
		// exhausted, and it would revert away from it.
		m.attempts = m.state[app->running] == LT_OTA_BANK_TRIAL ? LT_OTA_TRIAL_ATTEMPTS : 0;
		return lt_ota_meta_store(app->ops, &m);
	}

	uint32_t crc;
	if (!app->staged) {
		// Update.rollBack(), or a switch with no complete update staged
		if (!usable(app, &m, other))
			return false;
		if (stored == other)
			return true; // already switched
		crc = m.image_crc[other];
	} else if (lt_ota_image_check(app->ops, other, &crc) != LT_OTA_IMAGE_OK) {
		return false;
	}
	set_state(&m, other, LT_OTA_BANK_TRIAL, crc);
	record_running(app, &m);
	m.boot_bank = other;
	m.attempts	= LT_OTA_TRIAL_ATTEMPTS;
	return lt_ota_meta_store(app->ops, &m);
}

bool lt_ota_app_confirm(const lt_ota_app_t *app) {
	lt_ota_meta_t m;
	if (!lt_ota_meta_load(app->ops, &m))
		return true; // no metadata: nothing is on trial
	if (m.boot_bank != app->running || m.state[app->running] != LT_OTA_BANK_TRIAL)
		return true;
	// image_crc is left alone: the confirmed state stays bound to the image
	// the TRIAL was recorded for.
	m.state[app->running] = LT_OTA_BANK_CONFIRMED;
	m.attempts			  = 0;
	return lt_ota_meta_store(app->ops, &m);
}
