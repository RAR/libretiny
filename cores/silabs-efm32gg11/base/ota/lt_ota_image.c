/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "lt_ota_image.h"

#include "lt_crc32.h"

// Continue a CRC over [from, to) of a bank.
static bool crc_span(const lt_ota_flash_ops_t *ops, uint32_t base, uint32_t from, uint32_t to, uint32_t *acc) {
	uint8_t buf[256];
	while (from < to) {
		uint32_t n = to - from < sizeof(buf) ? to - from : sizeof(buf);
		if (ops->read(base + from, buf, n) != 0)
			return false;
		*acc = lt_crc32(*acc, buf, n);
		from += n;
	}
	return true;
}

lt_ota_image_err_t lt_ota_image_check(const lt_ota_flash_ops_t *ops, uint8_t bank, uint32_t *crc) {
	uint32_t bank_len = lt_ota_bank_length(bank);
	if (bank_len == 0)
		return LT_OTA_IMAGE_ERR_LENGTH;
	uint32_t base = lt_ota_bank_offset(bank);
	lt_ota_image_desc_t d;
	if (ops->read(base + LT_OTA_IMAGE_DESC_OFFSET, &d, sizeof(d)) != 0)
		return LT_OTA_IMAGE_ERR_READ;
	if (d.magic != LT_OTA_IMAGE_MAGIC || d.version < LT_OTA_IMAGE_VERSION || (d.crc_lo | d.crc_hi) > 0xFFFFu)
		return LT_OTA_IMAGE_ERR_NO_DESC;
	if (d.link_addr != base)
		return LT_OTA_IMAGE_ERR_WRONG_BANK;
	if (d.length < LT_OTA_IMAGE_DESC_OFFSET + sizeof(d) || d.length > bank_len)
		return LT_OTA_IMAGE_ERR_LENGTH;
	if (!lt_ota_vectors_ok(ops, bank, d.length))
		return LT_OTA_IMAGE_ERR_VECTORS;
	uint32_t acc = 0xFFFFFFFFu;
	if (!crc_span(ops, base, 0, LT_OTA_IMAGE_CRC_OFFSET, &acc) ||
		!crc_span(ops, base, LT_OTA_IMAGE_CRC_OFFSET + 8u, d.length, &acc))
		return LT_OTA_IMAGE_ERR_READ;
	acc ^= 0xFFFFFFFFu;
	if (acc != (d.crc_hi << 16 | d.crc_lo))
		return LT_OTA_IMAGE_ERR_CRC;
	if (crc)
		*crc = acc;
	return LT_OTA_IMAGE_OK;
}

bool lt_ota_vectors_ok(const lt_ota_flash_ops_t *ops, uint8_t bank, uint32_t len) {
	uint32_t vec[2]; // initial MSP, reset vector
	if (len < sizeof(vec) || len > lt_ota_bank_length(bank))
		return false;
	uint32_t base = lt_ota_bank_offset(bank);
	if (ops->read(base, vec, sizeof(vec)) != 0)
		return false;
	uint32_t msp = vec[0], pc = vec[1];
	if (msp <= LT_OTA_RAM_BASE || msp > LT_OTA_RAM_BASE + LT_OTA_RAM_SIZE || (msp & 0x3u))
		return false;
	if (!(pc & 1u))
		return false; // Cortex-M executes Thumb code only
	pc &= ~1u;
	return pc >= base + sizeof(vec) && pc < base + len;
}
