/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "lt_ota_meta.h"

#include "lt_crc32.h"

#include <string.h>

uint32_t lt_ota_bank_offset(uint8_t bank) {
	return bank == 1 ? (uint32_t)(FLASH_OTA2_OFFSET) : (uint32_t)(FLASH_OTA1_OFFSET);
}

uint32_t lt_ota_bank_length(uint8_t bank) {
	if (bank == 0)
		return (uint32_t)(FLASH_OTA1_LENGTH);
	if (bank == 1)
		return (uint32_t)(FLASH_OTA2_LENGTH);
	return 0;
}

int lt_ota_bank_of(uint32_t addr) {
	for (uint8_t bank = 0; bank < LT_OTA_BANK_COUNT; bank++) {
		uint32_t base = lt_ota_bank_offset(bank);
		if (addr >= base && addr - base < lt_ota_bank_length(bank))
			return bank;
	}
	return -1;
}

static uint32_t meta_crc(const lt_ota_meta_t *m) {
	return lt_crc32(0xFFFFFFFFu, m, offsetof(lt_ota_meta_t, crc32)) ^ 0xFFFFFFFFu;
}

static bool slot_load(const lt_ota_flash_ops_t *ops, uint32_t off, lt_ota_meta_t *out) {
	if (ops->read(off, out, sizeof(*out)) != 0)
		return false;
	return out->magic == LT_OTA_META_MAGIC && out->crc32 == meta_crc(out);
}

// Index (0/1) of the slot holding the newest valid record, or -1 if none.
static int newest_slot(const lt_ota_flash_ops_t *ops, lt_ota_meta_t *out) {
	lt_ota_meta_t s0, s1;
	bool v0 = slot_load(ops, LT_OTA_META_SLOT0_OFF, &s0);
	bool v1 = slot_load(ops, LT_OTA_META_SLOT1_OFF, &s1);
	int slot;
	if (v0 && v1)
		slot = (int32_t)(s1.seq - s0.seq) > 0 ? 1 : 0; // signed difference handles wraparound
	else if (v0)
		slot = 0;
	else if (v1)
		slot = 1;
	else
		return -1;
	if (out)
		*out = slot ? s1 : s0;
	return slot;
}

bool lt_ota_meta_load(const lt_ota_flash_ops_t *ops, lt_ota_meta_t *out) {
	return newest_slot(ops, out) >= 0;
}

bool lt_ota_meta_store(const lt_ota_flash_ops_t *ops, lt_ota_meta_t *m) {
	lt_ota_meta_t cur;
	int slot = newest_slot(ops, &cur);
	uint32_t target;
	if (slot < 0) {
		target = LT_OTA_META_SLOT0_OFF;
		m->seq = 1;
	} else {
		target = slot ? LT_OTA_META_SLOT0_OFF : LT_OTA_META_SLOT1_OFF;
		m->seq = cur.seq + 1;
	}
	m->magic = LT_OTA_META_MAGIC;
	m->crc32 = meta_crc(m);
	if (ops->erase(target, LT_OTA_META_SLOT_LEN) != 0)
		return false;
	if (ops->write(target, m, sizeof(*m)) != 0)
		return false;
	lt_ota_meta_t check;
	if (ops->read(target, &check, sizeof(check)) != 0)
		return false;
	return memcmp(&check, m, sizeof(check)) == 0;
}
