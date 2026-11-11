/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "lt_ota_boot.h"

#include <string.h>

typedef struct {
	const lt_ota_boot_checks_t *chk;
	const lt_ota_meta_t *meta; // record as loaded, before any update; NULL if none
	int8_t ok[LT_OTA_BANK_COUNT];
	uint32_t crc[LT_OTA_BANK_COUNT];
} boot_memo_t;

// A state the metadata offers for booting (anything but EMPTY or unknown).
static bool state_bootable(const lt_ota_meta_t *m, uint8_t bank) {
	return m->state[bank] >= LT_OTA_BANK_VALID && m->state[bank] <= LT_OTA_BANK_CONFIRMED;
}

// Bank verifies and, with metadata, is usable (see lt_ota_boot.h). The image
// check is cached: each is a CRC pass over a whole image.
static bool memo_ok(boot_memo_t *memo, uint8_t bank) {
	if (memo->meta && !state_bootable(memo->meta, bank))
		return false;
	if (memo->ok[bank] < 0)
		memo->ok[bank] = memo->chk->image_ok(memo->chk->arg, bank, &memo->crc[bank]) ? 1 : 0;
	if (memo->ok[bank] != 1)
		return false;
	return !memo->meta || memo->crc[bank] == memo->meta->image_crc[bank];
}

static bool is_trial(const lt_ota_meta_t *m, uint8_t bank) {
	return m->state[bank] == LT_OTA_BANK_TRIAL;
}

void lt_ota_boot_select(const lt_ota_meta_t *meta, const lt_ota_boot_checks_t *chk, lt_ota_boot_t *out) {
	memset(out, 0, sizeof(*out));
	out->bank		 = -1;
	uint8_t pref	 = 0; // preferred bank for the unverified fallback
	boot_memo_t memo = {
		.chk  = chk,
		.meta = meta,
		.ok	  = {-1, -1}
	};

	if (!meta) {
		// no metadata: the first bank that verifies, bank A first
		for (uint8_t bank = 0; bank < LT_OTA_BANK_COUNT; bank++) {
			if (memo_ok(&memo, bank)) {
				out->bank = bank;
				return;
			}
		}
	} else if (meta->boot_bank < LT_OTA_BANK_COUNT) {
		out->meta	  = *meta;
		uint8_t cur	  = meta->boot_bank;
		uint8_t other = cur ^ 1u;
		pref		  = cur;

		if (is_trial(meta, cur)) {
			if (meta->attempts > 0 && memo_ok(&memo, cur)) {
				out->meta.attempts--;
				out->store = true;
				out->bank  = cur;
				out->trial = true;
				return;
			}
			if (memo_ok(&memo, other)) {
				// trial exhausted or corrupt: revert and retire it
				out->meta.boot_bank		 = other;
				out->meta.attempts		 = 0;
				out->meta.state[cur]	 = LT_OTA_BANK_EMPTY;
				out->meta.image_crc[cur] = 0;
				out->store				 = true;
				out->bank				 = other;
				out->trial				 = is_trial(meta, other);
				return;
			}
			if (memo_ok(&memo, cur)) {
				// nothing to revert to: keep running the trial image
				out->bank  = cur;
				out->trial = true;
				return;
			}
		} else {
			if (memo_ok(&memo, cur)) {
				out->bank = cur;
				return;
			}
			if (memo_ok(&memo, other)) {
				out->bank  = other;
				out->trial = is_trial(meta, other);
				return;
			}
		}
	} else {
		// CRC-valid record with an impossible boot_bank: trust only verified images
		out->meta = *meta;
		for (uint8_t bank = 0; bank < LT_OTA_BANK_COUNT; bank++) {
			if (memo_ok(&memo, bank)) {
				out->bank  = bank;
				out->trial = is_trial(meta, bank);
				return;
			}
		}
	}

	// unverified fallback (see lt_ota_boot.h)
	if (chk->vectors_ok(chk->arg, pref))
		out->bank = pref;
	else if (chk->vectors_ok(chk->arg, pref ^ 1u))
		out->bank = pref ^ 1u;
}

static bool flash_image_ok(void *arg, uint8_t bank, uint32_t *crc) {
	return lt_ota_image_check((const lt_ota_flash_ops_t *)arg, bank, crc) == LT_OTA_IMAGE_OK;
}

static bool flash_vectors_ok(void *arg, uint8_t bank) {
	return lt_ota_vectors_ok((const lt_ota_flash_ops_t *)arg, bank, lt_ota_bank_length(bank));
}

int lt_ota_boot_run(const lt_ota_flash_ops_t *ops, bool *trial) {
	lt_ota_meta_t meta			   = {0};
	bool have_meta				   = lt_ota_meta_load(ops, &meta);
	const lt_ota_boot_checks_t chk = {.image_ok = flash_image_ok, .vectors_ok = flash_vectors_ok, .arg = (void *)ops};
	lt_ota_boot_t d;
	lt_ota_boot_select(have_meta ? &meta : NULL, &chk, &d);

	if (d.store && !lt_ota_meta_store(ops, &d.meta) && d.trial && d.bank == meta.boot_bank) {
		// the attempt could not be recorded: do not boot the trial image again
		uint8_t other = (uint8_t)d.bank ^ 1u;
		uint32_t crc;
		if (state_bootable(&meta, other) && flash_image_ok((void *)ops, other, &crc) && crc == meta.image_crc[other]) {
			*trial = is_trial(&meta, other);
			return other;
		}
	}
	*trial = d.trial;
	return d.bank;
}
