/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "host_flash.h"
#include "lt_crc32.h"
#include "lt_ota_meta.h"

#include <string.h>

static const lt_ota_flash_ops_t *ops = &host_flash_ops;

// Place a raw record with an arbitrary seq into a slot.
static void put_record(uint32_t slot_off, uint32_t seq, uint8_t boot_bank) {
	lt_ota_meta_t m;
	memset(&m, 0, sizeof(m));
	m.magic		= LT_OTA_META_MAGIC;
	m.seq		= seq;
	m.boot_bank = boot_bank;
	m.crc32		= lt_crc32(0xFFFFFFFFu, &m, offsetof(lt_ota_meta_t, crc32)) ^ 0xFFFFFFFFu;
	memset(host_flash_mem() + slot_off, 0xFF, LT_OTA_META_SLOT_LEN);
	memcpy(host_flash_mem() + slot_off, &m, sizeof(m));
}

static void test_layout(void) {
	CHECK(lt_ota_bank_offset(0) == FLASH_OTA1_OFFSET && lt_ota_bank_length(0) == FLASH_OTA1_LENGTH);
	CHECK(lt_ota_bank_offset(1) == FLASH_OTA2_OFFSET && lt_ota_bank_length(1) == FLASH_OTA2_LENGTH);
	CHECK(lt_ota_bank_length(2) == 0);
	CHECK(lt_ota_bank_of(FLASH_OTA1_OFFSET) == 0);
	CHECK(lt_ota_bank_of(FLASH_OTA1_OFFSET + FLASH_OTA1_LENGTH - 1) == 0);
	CHECK(lt_ota_bank_of(FLASH_OTA2_OFFSET) == 1);
	CHECK(lt_ota_bank_of(FLASH_OTA2_OFFSET + FLASH_OTA2_LENGTH - 1) == 1);
	CHECK(lt_ota_bank_of(FLASH_OTA2_OFFSET + FLASH_OTA2_LENGTH) == -1);
	CHECK(lt_ota_bank_of(0) == -1);
	CHECK(lt_ota_bank_of(FLASH_META_OFFSET) == -1);
	// metadata slots sit inside the meta partition, clear of both banks
	CHECK(LT_OTA_META_SLOT1_OFF + LT_OTA_META_SLOT_LEN <= FLASH_META_OFFSET + FLASH_META_LENGTH);
}

static void test_store_load(void) {
	host_flash_reset();
	lt_ota_meta_t m, r;
	CHECK(!lt_ota_meta_load(ops, &r)); // virgin flash

	memset(&m, 0, sizeof(m));
	m.boot_bank = 1;
	m.attempts	= 3;
	CHECK(lt_ota_meta_store(ops, &m));
	CHECK(lt_ota_meta_load(ops, &r));
	CHECK(r.seq == 1 && r.boot_bank == 1 && r.attempts == 3);
	CHECK(memcmp(host_flash_mem() + LT_OTA_META_SLOT0_OFF, &r, sizeof(r)) == 0);

	// next write alternates slots and wins
	m.boot_bank = 0;
	CHECK(lt_ota_meta_store(ops, &m));
	CHECK(lt_ota_meta_load(ops, &r));
	CHECK(r.seq == 2 && r.boot_bank == 0);
	CHECK(memcmp(host_flash_mem() + LT_OTA_META_SLOT1_OFF, &r, sizeof(r)) == 0);
	m.boot_bank = 1;
	CHECK(lt_ota_meta_store(ops, &m));
	CHECK(lt_ota_meta_load(ops, &r) && r.seq == 3 && r.boot_bank == 1);
	CHECK(memcmp(host_flash_mem() + LT_OTA_META_SLOT0_OFF, &r, sizeof(r)) == 0);
}

static void test_torn_write(void) {
	host_flash_reset();
	lt_ota_meta_t m, r;
	memset(&m, 0, sizeof(m));
	m.boot_bank = 0;
	CHECK(lt_ota_meta_store(ops, &m)); // seq 1 in slot 0

	// power fails after the erase of slot 1, before the write
	m.boot_bank		   = 1;
	host_fail_write_at = 1;
	CHECK(!lt_ota_meta_store(ops, &m));
	CHECK(lt_ota_meta_load(ops, &r) && r.seq == 1 && r.boot_bank == 0);

	// the retry targets the torn slot again and succeeds
	CHECK(lt_ota_meta_store(ops, &m));
	CHECK(lt_ota_meta_load(ops, &r) && r.seq == 2 && r.boot_bank == 1);

	// a failed erase leaves the older record in charge too
	m.boot_bank		   = 0;
	host_fail_erase_at = 1;
	CHECK(!lt_ota_meta_store(ops, &m));
	CHECK(lt_ota_meta_load(ops, &r) && r.seq == 2 && r.boot_bank == 1);

	// a partially programmed newest record (power lost mid-write) fails its CRC
	memset(host_flash_mem() + LT_OTA_META_SLOT1_OFF + 4, 0x00, 4); // seq
	CHECK(lt_ota_meta_load(ops, &r) && r.seq == 1 && r.boot_bank == 0);
}

static void test_readback(void) {
	host_flash_reset();
	lt_ota_meta_t m, r;
	memset(&m, 0, sizeof(m));
	CHECK(lt_ota_meta_store(ops, &m)); // slot 0
	m.boot_bank = 1;
	CHECK(lt_ota_meta_store(ops, &m)); // slot 1
	// slot 0 fails to erase silently: the old record ANDed with the new one is
	// detected by the read-back
	m.boot_bank		   = 0;
	m.attempts		   = 0xFF;
	host_fake_erase_at = 1;
	CHECK(!lt_ota_meta_store(ops, &m));
	CHECK(lt_ota_meta_load(ops, &r) && r.boot_bank == 1);
}

static void test_seq_wrap(void) {
	host_flash_reset();
	lt_ota_meta_t m, r;
	put_record(LT_OTA_META_SLOT0_OFF, 0xFFFFFFFEu, 0);
	put_record(LT_OTA_META_SLOT1_OFF, 0xFFFFFFFFu, 1);
	CHECK(lt_ota_meta_load(ops, &r) && r.seq == 0xFFFFFFFFu && r.boot_bank == 1);

	memset(&m, 0, sizeof(m));
	m.boot_bank = 0;
	CHECK(lt_ota_meta_store(ops, &m)); // replaces slot 0, seq wraps to 0
	CHECK(lt_ota_meta_load(ops, &r) && r.seq == 0 && r.boot_bank == 0);
	CHECK(memcmp(host_flash_mem() + LT_OTA_META_SLOT0_OFF, &r, sizeof(r)) == 0);

	m.boot_bank = 1;
	CHECK(lt_ota_meta_store(ops, &m)); // replaces slot 1 (0xFFFFFFFF), seq 1
	CHECK(lt_ota_meta_load(ops, &r) && r.seq == 1 && r.boot_bank == 1);
	CHECK(memcmp(host_flash_mem() + LT_OTA_META_SLOT1_OFF, &r, sizeof(r)) == 0);

	// only the second slot valid
	host_flash_reset();
	put_record(LT_OTA_META_SLOT1_OFF, 5, 1);
	CHECK(lt_ota_meta_load(ops, &r) && r.seq == 5);
	CHECK(lt_ota_meta_store(ops, &m)); // goes to slot 0
	CHECK(lt_ota_meta_load(ops, &r) && r.seq == 6);
	CHECK(memcmp(host_flash_mem() + LT_OTA_META_SLOT0_OFF, &r, sizeof(r)) == 0);
}

// A format-1 record ("OTA1", 40 bytes: per-bank length/CRC/state) as written by
// earlier firmware, with a valid CRC.
static void put_v1_record(uint32_t slot_off, uint32_t seq, uint8_t boot_bank) {
	uint8_t rec[40];
	memset(rec, 0, sizeof(rec));
	uint32_t magic = 0x4F544131u;
	memcpy(rec, &magic, 4);
	memcpy(rec + 4, &seq, 4);
	rec[8]		 = boot_bank;
	rec[13]		 = 0x10;			  // bank A length 0x1000
	rec[20]		 = LT_OTA_BANK_VALID; // bank A state
	uint32_t crc = lt_crc32(0xFFFFFFFFu, rec, 36) ^ 0xFFFFFFFFu;
	memcpy(rec + 36, &crc, 4);
	memset(host_flash_mem() + slot_off, 0xFF, LT_OTA_META_SLOT_LEN);
	memcpy(host_flash_mem() + slot_off, rec, sizeof(rec));
}

static void test_old_format(void) {
	host_flash_reset();
	lt_ota_meta_t m, r;
	put_v1_record(LT_OTA_META_SLOT0_OFF, 7, 1);
	CHECK(!lt_ota_meta_load(ops, &r)); // not read: no metadata
	put_v1_record(LT_OTA_META_SLOT1_OFF, 8, 0);
	CHECK(!lt_ota_meta_load(ops, &r));

	// the first new-format write starts over in slot 0 and is the only valid record
	memset(&m, 0, sizeof(m));
	m.boot_bank = 1;
	CHECK(lt_ota_meta_store(ops, &m));
	CHECK(lt_ota_meta_load(ops, &r) && r.seq == 1 && r.boot_bank == 1);
	CHECK(memcmp(host_flash_mem() + LT_OTA_META_SLOT0_OFF, &r, sizeof(r)) == 0);
	// the next one replaces the old record in slot 1
	m.boot_bank = 0;
	CHECK(lt_ota_meta_store(ops, &m));
	CHECK(lt_ota_meta_load(ops, &r) && r.seq == 2 && r.boot_bank == 0);
	CHECK(memcmp(host_flash_mem() + LT_OTA_META_SLOT1_OFF, &r, sizeof(r)) == 0);
}

int main(void) {
	test_layout();
	test_store_load();
	test_torn_write();
	test_readback();
	test_seq_wrap();
	test_old_format();
	return 0;
}
