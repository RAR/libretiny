/* Copyright (c) Andrew Rankin 2026-06-12. */

/*
 * Test images: a plausible vector table, a seeded byte pattern, and an image
 * descriptor at LT_OTA_IMAGE_DESC_OFFSET filled in the way the builder does
 * (builder/utils/efm32gg11_image.py; test_pyimage checks the two agree).
 */

#pragma once

#include "host_flash.h"
#include "lt_crc32.h"
#include "lt_ota_image.h"
#include "lt_ota_meta.h"

#include <string.h>

// Shortest image that holds a descriptor.
#define IMG_MIN_LEN (LT_OTA_IMAGE_DESC_OFFSET + sizeof(lt_ota_image_desc_t))

// CRC-32 of an image as the descriptor defines it: the crc_lo/crc_hi words left out.
static inline uint32_t image_crc(const uint8_t *img, uint32_t len) {
	uint32_t acc = lt_crc32(0xFFFFFFFFu, img, LT_OTA_IMAGE_CRC_OFFSET);
	acc			 = lt_crc32(acc, img + LT_OTA_IMAGE_CRC_OFFSET + 8, len - LT_OTA_IMAGE_CRC_OFFSET - 8);
	return acc ^ 0xFFFFFFFFu;
}

// Fill in the descriptor of an image of len (>= IMG_MIN_LEN) bytes linked for link_bank.
static inline void image_desc(uint8_t *buf, uint32_t len, uint8_t link_bank) {
	lt_ota_image_desc_t d = {
		.magic	   = LT_OTA_IMAGE_MAGIC,
		.version   = LT_OTA_IMAGE_VERSION,
		.link_addr = lt_ota_bank_offset(link_bank),
		.length	   = len,
	};
	memcpy(buf + LT_OTA_IMAGE_DESC_OFFSET, &d, sizeof(d));
	uint32_t crc = image_crc(buf, len);
	d.crc_lo	 = crc & 0xFFFFu;
	d.crc_hi	 = crc >> 16;
	memcpy(buf + LT_OTA_IMAGE_DESC_OFFSET, &d, sizeof(d));
}

// An image of len bytes linked for link_bank. Images shorter than
// IMG_MIN_LEN get no descriptor.
static inline void image_bytes(uint8_t *buf, uint32_t len, uint32_t seed, uint8_t link_bank) {
	uint32_t vec[2] = {LT_OTA_RAM_BASE + LT_OTA_RAM_SIZE, lt_ota_bank_offset(link_bank) + 0x100 + 1};
	uint32_t x		= seed * 2654435761u + 1;
	for (uint32_t i = 0; i < len; i++) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		buf[i] = (uint8_t)x;
	}
	memcpy(buf, vec, len < sizeof(vec) ? len : sizeof(vec));
	if (len >= IMG_MIN_LEN)
		image_desc(buf, len, link_bank);
}

// Write an image straight into a bank, as an SWD programmer would. Returns its CRC.
static inline uint32_t flash_image(uint8_t bank, uint32_t len, uint32_t seed) {
	uint8_t *mem = host_flash_mem() + lt_ota_bank_offset(bank);
	memset(mem, 0xFF, lt_ota_bank_length(bank));
	image_bytes(mem, len, seed, bank);
	return image_crc(mem, len);
}

// Stage an image into a bank through the flash ops, as an OTA update would.
// Returns the staged length the FAL port reports for a complete update: the end
// of the last write, not rounded (the port pads the last word with 0xFF).
static inline uint32_t stage_image(uint8_t bank, uint32_t len, uint32_t seed, uint8_t link_bank) {
	static uint8_t buf[0x100000];
	uint32_t base  = lt_ota_bank_offset(bank);
	uint32_t words = (len + 3u) & ~3u;
	memset(buf, 0xFF, words);
	image_bytes(buf, len, seed, link_bank);
	CHECK(host_flash_ops.erase(base, (words + HOST_FLASH_PAGE - 1) & ~(HOST_FLASH_PAGE - 1)) == 0);
	CHECK(host_flash_ops.write(base, buf, words) == 0);
	return len;
}

// An update into a bank that stopped after the first `written` bytes of an
// image of len bytes (the pages it reached are erased, the rest keep old data).
static inline void stage_partial(uint8_t bank, uint32_t len, uint32_t written, uint32_t seed, uint8_t link_bank) {
	static uint8_t buf[0x100000];
	uint32_t base = lt_ota_bank_offset(bank);
	image_bytes(buf, len, seed, link_bank);
	written = (written + 3u) & ~3u;
	CHECK(host_flash_ops.erase(base, (written + HOST_FLASH_PAGE - 1) & ~(HOST_FLASH_PAGE - 1)) == 0);
	CHECK(host_flash_ops.write(base, buf, written) == 0);
}

// Clear bits inside an image, the only corruption NOR flash can suffer
// without an erase.
static inline void corrupt_bank(uint8_t bank, uint32_t at) {
	host_flash_mem()[lt_ota_bank_offset(bank) + at] &= 0x0F;
	host_flash_mem()[lt_ota_bank_offset(bank) + at + 1] &= 0xF0;
}

// Flip one bit anywhere in a bank (also in the direction NOR cannot, as a
// corrupted download would).
static inline void flip_bit(uint8_t bank, uint32_t at, uint8_t bit) {
	host_flash_mem()[lt_ota_bank_offset(bank) + at] ^= (uint8_t)(1u << bit);
}

// lt_ota_image_check() on the host flash.
static inline lt_ota_image_err_t check_bank(uint8_t bank) {
	return lt_ota_image_check(&host_flash_ops, bank, NULL);
}
