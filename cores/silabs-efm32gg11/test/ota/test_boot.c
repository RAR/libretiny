/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "host_flash.h"
#include "lt_ota_boot.h"
#include "test_image.h"

#include <string.h>

static const lt_ota_flash_ops_t *ops = &host_flash_ops;

// --- lt_ota_boot_select() with scripted check results ---

typedef struct {
	bool image[2], vectors[2];
	uint32_t crc[2]; // descriptor CRC of each bank's image; meta() records 0
	unsigned image_calls[2], vector_calls[2];
} fake_t;

static bool fake_image_ok(void *arg, uint8_t bank, uint32_t *crc) {
	fake_t *f = arg;
	CHECK(bank < 2);
	f->image_calls[bank]++;
	*crc = f->crc[bank];
	return f->image[bank];
}

static bool fake_vectors_ok(void *arg, uint8_t bank) {
	fake_t *f = arg;
	CHECK(bank < 2);
	f->vector_calls[bank]++;
	return f->vectors[bank];
}

static lt_ota_meta_t meta(uint8_t boot, uint8_t attempts, uint8_t state_a, uint8_t state_b) {
	lt_ota_meta_t m;
	memset(&m, 0, sizeof(m));
	m.magic		= LT_OTA_META_MAGIC;
	m.boot_bank = boot;
	m.attempts	= attempts;
	m.state[0]	= state_a;
	m.state[1]	= state_b;
	return m;
}

static lt_ota_boot_t select_with(const lt_ota_meta_t *m, fake_t *f) {
	lt_ota_boot_checks_t chk = {fake_image_ok, fake_vectors_ok, f};
	lt_ota_boot_t d;
	lt_ota_boot_select(m, &chk, &d);
	for (int b = 0; b < 2; b++)
		CHECK(f->image_calls[b] <= 1 && f->vector_calls[b] <= 1); // at most one CRC pass per bank
	return d;
}

static void test_select_no_meta(void) {
	lt_ota_boot_t d;

	// zero banks verify: first bank with sane vectors, A first, never trial
	fake_t f = {
		.vectors = {true, true}
	};
	d = select_with(NULL, &f);
	CHECK(d.bank == 0 && !d.trial && !d.store && f.image_calls[0] == 1 && f.image_calls[1] == 1);
	f = (fake_t){
		.vectors = {false, true}
	};
	d = select_with(NULL, &f);
	CHECK(d.bank == 1 && !d.trial && !d.store);
	f = (fake_t){0};
	d = select_with(NULL, &f);
	CHECK(d.bank == -1);

	// one bank verifies: it boots, whichever it is, even if the other has sane vectors
	f = (fake_t){
		.image	 = {false, true},
		.vectors = {true,  true}
	};
	d = select_with(NULL, &f);
	CHECK(d.bank == 1 && !d.trial && !d.store && f.vector_calls[0] == 0);
	f = (fake_t){
		.image	 = {true, false},
		.vectors = {true, true }
	};
	d = select_with(NULL, &f);
	CHECK(d.bank == 0 && !d.trial && !d.store && f.image_calls[1] == 0);

	// two banks verify: bank A
	f = (fake_t){
		.image	 = {true, true},
		.vectors = {true, true}
	};
	d = select_with(NULL, &f);
	CHECK(d.bank == 0 && !d.trial && !d.store && f.image_calls[1] == 0);
}

static void test_select(void) {
	lt_ota_meta_t m;
	lt_ota_boot_t d;
	fake_t f;

	// confirmed image boots without a write
	m = meta(1, 0, LT_OTA_BANK_VALID, LT_OTA_BANK_CONFIRMED);
	f = (fake_t){
		.image	 = {true, true},
		.vectors = {true, true}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 1 && !d.trial && !d.store && f.image_calls[0] == 0);

	// confirmed image corrupt: the other verified bank boots, nothing stored
	f = (fake_t){
		.image	 = {true, false},
		.vectors = {true, true }
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 0 && !d.trial && !d.store);

	// trial with attempts: one is consumed
	m = meta(1, 3, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
	f = (fake_t){
		.image	 = {true, true},
		.vectors = {true, true}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 1 && d.trial && d.store && d.meta.attempts == 2 && d.meta.boot_bank == 1);
	CHECK(f.image_calls[0] == 0); // the fallback bank is not checked on the happy path

	// trial exhausted: revert, retire the trial
	m = meta(1, 0, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
	d = select_with(&m, &f);
	CHECK(d.bank == 0 && !d.trial && d.store && d.meta.boot_bank == 0 && d.meta.attempts == 0);
	CHECK(d.meta.state[1] == LT_OTA_BANK_EMPTY && d.meta.state[0] == LT_OTA_BANK_VALID);

	// trial image corrupt with attempts left: revert immediately
	m = meta(1, 2, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
	f = (fake_t){
		.image	 = {true, false},
		.vectors = {true, true }
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 0 && d.store && d.meta.boot_bank == 0 && d.meta.state[1] == LT_OTA_BANK_EMPTY);

	// trial exhausted, nothing to revert to: keep booting it, watchdog armed, no write
	m = meta(1, 0, LT_OTA_BANK_EMPTY, LT_OTA_BANK_TRIAL);
	f = (fake_t){
		.image	 = {true, true},
		.vectors = {true, true}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 1 && d.trial && !d.store);
	CHECK(f.image_calls[0] == 0); // an EMPTY bank is never checked, even if it would verify

	// both banks fail verification: unverified fallback, preferring boot_bank
	m = meta(1, 0, LT_OTA_BANK_VALID, LT_OTA_BANK_VALID);
	f = (fake_t){
		.vectors = {true, true}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 1 && !d.trial && !d.store);
	f = (fake_t){
		.vectors = {true, false}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 0 && !d.trial && !d.store);
	f = (fake_t){0};
	d = select_with(&m, &f);
	CHECK(d.bank == -1);
	m = meta(1, 2, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
	f = (fake_t){
		.vectors = {true, true}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 1 && !d.trial && !d.store);

	// boot_bank out of range: only verified banks are trusted
	m = meta(7, 3, LT_OTA_BANK_VALID, LT_OTA_BANK_VALID);
	f = (fake_t){
		.image	 = {false, true},
		.vectors = {true,  true}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 1 && !d.store);
	f = (fake_t){
		.vectors = {true, true}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 0 && !d.store);

	// boot_bank recorded as EMPTY: the other verified bank wins, whatever boot_bank holds
	m = meta(0, 0, LT_OTA_BANK_EMPTY, LT_OTA_BANK_VALID);
	f = (fake_t){
		.image	 = {true, true},
		.vectors = {true, true}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 1 && !d.store && f.image_calls[0] == 0);

	// an unknown state counts as EMPTY
	m = meta(0, 0, 9, LT_OTA_BANK_VALID);
	d = select_with(
		&m,
		&(fake_t){
			.image	 = {true, true},
			.vectors = {true, true}
	}
	);
	CHECK(d.bank == 1);

	// a verified image that is not the one its state was recorded for counts as EMPTY
	m = meta(1, 0, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
	f = (fake_t){
		.image	 = {true, true},
		.vectors = {true, true},
		.crc	 = {7,	   0	}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 1 && d.trial && !d.store); // exhausted trial, nothing to revert to
	m = meta(0, 0, LT_OTA_BANK_CONFIRMED, LT_OTA_BANK_VALID);
	f = (fake_t){
		.image	 = {true, true},
		.vectors = {true, true},
		.crc	 = {7,	   0	}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 1 && !d.trial && !d.store);
	f = (fake_t){
		.image	 = {true, true},
		.vectors = {true, true},
		.crc	 = {7,	   7	}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 0 && !d.store); // last resort, boot_bank first
	m = meta(1, 3, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
	f = (fake_t){
		.image	 = {true, true},
		.vectors = {true, true},
		.crc	 = {0,	   7	}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 0 && d.store && d.meta.state[1] == LT_OTA_BANK_EMPTY); // trial bank rewritten: reverted
	m.image_crc[1] = 7;
	f			   = (fake_t){
					 .image	  = {true, true},
					 .vectors = {true, true},
					 .crc	  = {0,	7	 }
	 };
	d = select_with(&m, &f);
	CHECK(d.bank == 1 && d.trial && d.store);

	// reverting to a bank that is itself TRIAL keeps the watchdog
	m = meta(0, 0, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
	f = (fake_t){
		.image	 = {false, true},
		.vectors = {true,  true}
	};
	d = select_with(&m, &f);
	CHECK(d.bank == 1 && d.trial && !d.store);
}

// --- lt_ota_boot_run() against the flash model ---

// Descriptor CRC of whatever image a bank holds now (read, not verified).
static uint32_t desc_crc(uint8_t bank) {
	const lt_ota_image_desc_t *d =
		(const lt_ota_image_desc_t *)(host_flash_mem() + lt_ota_bank_offset(bank) + LT_OTA_IMAGE_DESC_OFFSET);
	return d->crc_hi << 16 | d->crc_lo;
}

// A record whose states belong to the images in the banks now.
static lt_ota_meta_t make_meta(uint8_t boot, uint8_t attempts, uint8_t st_a, uint8_t st_b) {
	lt_ota_meta_t m;
	memset(&m, 0, sizeof(m));
	m.boot_bank	   = boot;
	m.attempts	   = attempts;
	m.state[0]	   = st_a;
	m.state[1]	   = st_b;
	m.image_crc[0] = desc_crc(0);
	m.image_crc[1] = desc_crc(1);
	return m;
}

static void erase_meta(void) {
	memset(host_flash_mem() + FLASH_META_OFFSET, 0xFF, FLASH_META_LENGTH);
}

static void test_run_no_meta(void) {
	bool trial;

	// virgin flash: nothing bootable
	host_flash_reset();
	CHECK(lt_ota_boot_run(ops, &trial) == -1);

	// SWD-flashed bank A, metadata erased: boots A, writes nothing
	flash_image(0, 0x4000, 1);
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);
	CHECK(host_write_count == 0 && host_erase_count == 0);

	// a single image flashed into bank B: boots B
	host_flash_reset();
	flash_image(1, 0x4000, 2);
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && !trial);
	// ...also when bank A holds something with sane vectors that does not verify
	flash_image(0, 0x4000, 1);
	corrupt_bank(0, 0x2000);
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && !trial);
	// ...or an image linked for bank B
	stage_image(0, 0x4000, 3, 1);
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && !trial);

	// both banks verify: bank A
	flash_image(0, 0x4000, 1);
	unsigned writes = host_write_count;
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);
	CHECK(host_write_count == writes);

	// neither verifies: unverified fallback by vector table, A first
	corrupt_bank(0, 0x100);
	corrupt_bank(1, 0x100);
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);
	memset(host_flash_mem() + lt_ota_bank_offset(0), 0x00, 8);
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && !trial);

	// an old-format record counts as no metadata: its boot_bank (B) is ignored
	host_flash_reset();
	flash_image(0, 0x4000, 1);
	flash_image(1, 0x4000, 2);
	uint8_t rec[40] = {0x31, 0x41, 0x54, 0x4F, 1, 0, 0, 0, 1}; // "OTA1", seq 1, boot_bank 1
	uint32_t crc	= lt_crc32(0xFFFFFFFFu, rec, 36) ^ 0xFFFFFFFFu;
	memcpy(rec + 36, &crc, 4);
	memcpy(host_flash_mem() + LT_OTA_META_SLOT0_OFF, rec, sizeof(rec));
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);
	CHECK(host_write_count == 0);
}

static void test_run(void) {
	bool trial;
	lt_ota_meta_t m, r;

	// a trial countdown, then the revert
	host_flash_reset();
	flash_image(0, 0x4000, 1);
	flash_image(1, 0x4000, 2);
	m = make_meta(1, 2, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
	CHECK(lt_ota_meta_store(ops, &m));
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && trial);
	CHECK(lt_ota_meta_load(ops, &r) && r.attempts == 1 && r.boot_bank == 1);
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && trial);
	CHECK(lt_ota_meta_load(ops, &r) && r.attempts == 0 && r.boot_bank == 1);
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);
	CHECK(lt_ota_meta_load(ops, &r) && r.boot_bank == 0 && r.state[1] == LT_OTA_BANK_EMPTY);
	unsigned writes = host_write_count;
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);
	CHECK(host_write_count == writes); // steady state: no flash wear
	// the retired image still verifies by its descriptor, but is never booted
	CHECK(check_bank(1) == LT_OTA_IMAGE_OK);
	corrupt_bank(0, 0x3000);
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial); // unverified fallback prefers boot_bank

	// the attempt cannot be recorded: boot the verified other bank instead
	flash_image(0, 0x4000, 1);
	m = make_meta(1, 3, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
	CHECK(lt_ota_meta_store(ops, &m));
	host_fail_write_at = 1;
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);
	CHECK(lt_ota_meta_load(ops, &r) && r.attempts == 3 && r.boot_bank == 1); // previous record kept
	// ...and with nothing to fall back to, the trial still boots
	corrupt_bank(0, 0x3000);
	host_fail_write_at = 1;
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && trial);

	// both images corrupt behind valid metadata: unverified boot of boot_bank
	host_flash_reset();
	flash_image(0, 0x4000, 1);
	flash_image(1, 0x4000, 2);
	m = make_meta(1, 0, LT_OTA_BANK_VALID, LT_OTA_BANK_CONFIRMED);
	CHECK(lt_ota_meta_store(ops, &m));
	corrupt_bank(0, 0x100);
	corrupt_bank(1, 0x100);
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && !trial);
	memset(host_flash_mem() + lt_ota_bank_offset(1), 0x00, 8); // and B's vector table gone
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);

	// torn metadata write during the countdown: the older record is used
	host_flash_reset();
	flash_image(0, 0x4000, 1);
	flash_image(1, 0x4000, 2);
	m = make_meta(1, 3, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
	CHECK(lt_ota_meta_store(ops, &m));
	memset(host_flash_mem() + LT_OTA_META_SLOT1_OFF, 0xFF, LT_OTA_META_SLOT_LEN); // stale slot erased
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && trial);
	// power lost after erasing the slot: the decrement is lost, attempts restart from 3
	memset(host_flash_mem() + LT_OTA_META_SLOT1_OFF, 0xFF, LT_OTA_META_SLOT_LEN);
	CHECK(lt_ota_meta_load(ops, &r) && r.attempts == 3);
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && trial);
	CHECK(lt_ota_meta_load(ops, &r) && r.attempts == 2);

	// out-of-range boot_bank in a CRC-valid record
	host_flash_reset();
	flash_image(0, 0x4000, 1);
	flash_image(1, 0x4000, 2);
	m = make_meta(5, 0, LT_OTA_BANK_VALID, LT_OTA_BANK_VALID);
	CHECK(lt_ota_meta_store(ops, &m));
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);
	corrupt_bank(0, 0x200);
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && !trial);
}

static void test_run_bad_images(void) {
	bool trial;
	lt_ota_meta_t m;

	// boot_bank TRIAL holding a truncated, bit-flipped, wrongly linked or
	// descriptor-damaged image: revert to the verified bank, retire the trial
	for (int kind = 0; kind < 4; kind++) {
		host_flash_reset();
		flash_image(0, 0x4000, 1);
		flash_image(1, 0x6000, 2);
		switch (kind) {
			case 0: // truncated
				memset(host_flash_mem() + lt_ota_bank_offset(1) + 0x5000, 0xFF, 0x1000);
				break;
			case 1: // one bit flipped in the body
				flip_bit(1, 0x2345, 3);
				break;
			case 2: // linked for bank A
				stage_image(1, 0x6000, 2, 0);
				break;
			case 3: // one bit flipped in the descriptor's length
				flip_bit(1, LT_OTA_IMAGE_DESC_OFFSET + 12, 2);
				break;
		}
		CHECK(check_bank(1) != LT_OTA_IMAGE_OK);
		m = make_meta(1, 3, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
		CHECK(lt_ota_meta_store(ops, &m));
		CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);
		CHECK(lt_ota_meta_load(ops, &m) && m.boot_bank == 0 && m.state[1] == LT_OTA_BANK_EMPTY);
	}
}

// Images the metadata does not know: a state never passes to a new image.
static void test_run_unknown_images(void) {
	bool trial;
	lt_ota_meta_t m, r;

	// Running B as an unconfirmed trial, A VALID (v1). An update writes v3
	// completely into A, and power is lost before its activation is stored.
	host_flash_reset();
	flash_image(0, 0x4000, 1);
	flash_image(1, 0x4000, 2);
	m = make_meta(1, 3, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
	CHECK(lt_ota_meta_store(ops, &m));
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && trial);
	stage_image(0, 0x5000, 3, 0);
	CHECK(check_bank(0) == LT_OTA_IMAGE_OK);
	// B's attempts run out: v3 is not v1, so there is nothing to revert to;
	// B keeps booting as a trial (watchdog armed) and is not retired
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && trial);
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && trial);
	for (int i = 0; i < 3; i++)
		CHECK(lt_ota_boot_run(ops, &trial) == 1 && trial);
	CHECK(lt_ota_meta_load(ops, &r) && r.boot_bank == 1 && r.state[1] == LT_OTA_BANK_TRIAL && r.attempts == 0);
	// ...and if B fails as well, nothing is usable: v3 boots only through the
	// last resort (vector table, boot_bank first, unwatched, nothing stored)
	corrupt_bank(1, 0x2000);
	unsigned writes = host_write_count;
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && !trial); // vectors of boot_bank first
	memset(host_flash_mem() + lt_ota_bank_offset(1), 0x00, 8);
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);
	CHECK(host_write_count == writes);

	// boot_bank re-flashed directly with meta intact: the other bank, still the
	// image its state was recorded for, boots
	host_flash_reset();
	flash_image(0, 0x4000, 1);
	flash_image(1, 0x4000, 2);
	m = make_meta(1, 0, LT_OTA_BANK_VALID, LT_OTA_BANK_CONFIRMED);
	CHECK(lt_ota_meta_store(ops, &m));
	flash_image(1, 0x5000, 3);
	CHECK(check_bank(1) == LT_OTA_IMAGE_OK);
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);
	// ...the other one too: nothing is usable, the last resort boots boot_bank
	flash_image(0, 0x5000, 4);
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && !trial);
	// ...the same image written again is the same image: its state applies
	host_flash_reset();
	flash_image(0, 0x4000, 1);
	flash_image(1, 0x4000, 2);
	m = make_meta(1, 2, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
	CHECK(lt_ota_meta_store(ops, &m));
	flash_image(1, 0x4000, 2);
	CHECK(lt_ota_boot_run(ops, &trial) == 1 && trial);
	CHECK(lt_ota_meta_load(ops, &r) && r.attempts == 1);
	// erasing meta is the cure: both verify, bank A boots
	flash_image(0, 0x5000, 4);
	erase_meta();
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);

	// the retire clears the identity too
	host_flash_reset();
	flash_image(0, 0x4000, 1);
	flash_image(1, 0x4000, 2);
	m = make_meta(1, 0, LT_OTA_BANK_VALID, LT_OTA_BANK_TRIAL);
	CHECK(lt_ota_meta_store(ops, &m));
	CHECK(lt_ota_boot_run(ops, &trial) == 0 && !trial);
	CHECK(lt_ota_meta_load(ops, &r) && r.state[1] == LT_OTA_BANK_EMPTY && r.image_crc[1] == 0);
	CHECK(r.image_crc[0] == desc_crc(0));
}

int main(void) {
	test_select_no_meta();
	test_select();
	test_run_no_meta();
	test_run();
	test_run_bad_images();
	test_run_unknown_images();
	return 0;
}
