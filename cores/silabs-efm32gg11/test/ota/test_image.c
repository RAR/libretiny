/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "test_image.h"
#include "host_flash.h"
#include "lt_ota_image.h"

#include <stddef.h>
#include <string.h>

static const lt_ota_flash_ops_t *ops = &host_flash_ops;

static lt_ota_image_desc_t *desc_of(uint8_t bank) {
	return (lt_ota_image_desc_t *)(host_flash_mem() + lt_ota_bank_offset(bank) + LT_OTA_IMAGE_DESC_OFFSET);
}

// Recompute the CRC words after changing covered bytes of an image in a bank.
static void refill_crc(uint8_t bank, uint32_t len) {
	uint32_t crc		  = image_crc(host_flash_mem() + lt_ota_bank_offset(bank), len);
	desc_of(bank)->crc_lo = crc & 0xFFFFu;
	desc_of(bank)->crc_hi = crc >> 16;
}

static void test_vectors(void) {
	host_flash_reset();
	CHECK(!lt_ota_vectors_ok(ops, 0, lt_ota_bank_length(0))); // erased bank
	flash_image(0, 0x1000, 1);
	CHECK(lt_ota_vectors_ok(ops, 0, 0x1000));
	CHECK(!lt_ota_vectors_ok(ops, 0, 0x100));					  // reset vector past the image end
	CHECK(!lt_ota_vectors_ok(ops, 0, 4));						  // shorter than the vector table
	CHECK(!lt_ota_vectors_ok(ops, 0, lt_ota_bank_length(0) + 4)); // longer than the bank

	uint32_t *vec = (uint32_t *)(host_flash_mem() + lt_ota_bank_offset(0));
	vec[0]		  = LT_OTA_RAM_BASE; // empty stack
	CHECK(!lt_ota_vectors_ok(ops, 0, 0x1000));
	vec[0] = LT_OTA_RAM_BASE + LT_OTA_RAM_SIZE + 4; // past SRAM
	CHECK(!lt_ota_vectors_ok(ops, 0, 0x1000));
	vec[0] = LT_OTA_RAM_BASE + 0x1002; // misaligned
	CHECK(!lt_ota_vectors_ok(ops, 0, 0x1000));
	vec[0] = LT_OTA_RAM_BASE + 0x1000;
	CHECK(lt_ota_vectors_ok(ops, 0, 0x1000));
	vec[1] &= ~1u; // ARM-state reset vector
	CHECK(!lt_ota_vectors_ok(ops, 0, 0x1000));

	// an image linked for bank A placed in bank B
	stage_image(1, 0x1000, 2, 0);
	CHECK(!lt_ota_vectors_ok(ops, 1, 0x1000));
	stage_image(1, 0x1000, 2, 1);
	CHECK(lt_ota_vectors_ok(ops, 1, 0x1000));
}

static void test_good(void) {
	host_flash_reset();
	uint32_t crc, want;
	want = flash_image(0, 0x3001, 7); // odd length: the last word is padded with 0xFF
	CHECK(lt_ota_image_check(ops, 0, &crc) == LT_OTA_IMAGE_OK && crc == want);
	want = flash_image(1, IMG_MIN_LEN, 8); // nothing after the descriptor
	CHECK(lt_ota_image_check(ops, 1, &crc) == LT_OTA_IMAGE_OK && crc == want);
	// a full bank
	want = flash_image(1, lt_ota_bank_length(1), 9);
	CHECK(lt_ota_image_check(ops, 1, &crc) == LT_OTA_IMAGE_OK && crc == want);
	// the CRC reported is the descriptor's
	CHECK(crc == (desc_of(1)->crc_hi << 16 | desc_of(1)->crc_lo));
	// a later descriptor version that appended a field (covered by the CRC)
	desc_of(0)->version = LT_OTA_IMAGE_VERSION + 1;
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_CRC); // the version is covered
	refill_crc(0, 0x3001);
	CHECK(check_bank(0) == LT_OTA_IMAGE_OK);
	// bank index out of range
	CHECK(lt_ota_image_check(ops, 2, NULL) == LT_OTA_IMAGE_ERR_LENGTH);
}

static void test_no_desc(void) {
	host_flash_reset();
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_NO_DESC); // erased bank
	flash_image(0, 0x2000, 1);
	uint8_t *mem = host_flash_mem() + lt_ota_bank_offset(0);
	// descriptor as linked, before the builder filled in the CRC (e.g. a raw ELF
	// made without the post-link step)
	desc_of(0)->crc_lo = desc_of(0)->crc_hi = 0xFFFFFFFFu;
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_NO_DESC);
	// a version older than this reader knows
	image_desc(mem, 0x2000, 0);
	desc_of(0)->version = 0;
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_NO_DESC);
	// an image without a descriptor (e.g. built before descriptors existed)
	flash_image(0, 0x2000, 1);
	memset(mem + LT_OTA_IMAGE_DESC_OFFSET, 0, sizeof(lt_ota_image_desc_t));
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_NO_DESC);
	CHECK(lt_ota_vectors_ok(ops, 0, lt_ota_bank_length(0))); // still bootable as a last resort
}

static void test_wrong_bank(void) {
	host_flash_reset();
	stage_image(1, 0x2000, 3, 0); // bank A image in bank B
	CHECK(check_bank(1) == LT_OTA_IMAGE_ERR_WRONG_BANK);
	stage_image(0, 0x2000, 3, 1); // bank B image in bank A
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_WRONG_BANK);
	// only the descriptor's link address is wrong
	stage_image(0, 0x2000, 3, 0);
	desc_of(0)->link_addr = lt_ota_bank_offset(1);
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_WRONG_BANK);
}

static void test_length(void) {
	host_flash_reset();
	uint8_t *mem = host_flash_mem() + lt_ota_bank_offset(0);
	flash_image(0, 0x2000, 4);
	desc_of(0)->length = lt_ota_bank_length(0) + 4;
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_LENGTH);
	desc_of(0)->length = IMG_MIN_LEN - 4; // ends inside the descriptor
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_LENGTH);
	desc_of(0)->length = 0;
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_LENGTH);
	// a length that leaves the reset vector outside the image
	flash_image(0, 0x2000, 4);
	((uint32_t *)mem)[1] = lt_ota_bank_offset(0) + 0x3001;
	image_desc(mem, 0x2000, 0);
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_VECTORS);
}

static void test_truncated(void) {
	// an update cut short: the end of the image never arrived (erased flash)
	host_flash_reset();
	stage_image(1, 0x6000, 5, 1);
	CHECK(check_bank(1) == LT_OTA_IMAGE_OK);
	memset(host_flash_mem() + lt_ota_bank_offset(1) + 0x5000, 0xFF, 0x1000);
	CHECK(check_bank(1) == LT_OTA_IMAGE_ERR_CRC);
	// cut short over an older, longer image: the tail is stale data
	host_flash_reset();
	stage_image(1, 0x6000, 5, 1);
	uint8_t newer[0x3000];
	image_bytes(newer, sizeof(newer), 6, 1);
	memcpy(host_flash_mem() + lt_ota_bank_offset(1), newer, 0x1000); // first page of the new image only
	CHECK(check_bank(1) == LT_OTA_IMAGE_ERR_CRC);
	// the last byte wrong
	host_flash_reset();
	flash_image(0, 0x2001, 6);
	host_flash_mem()[lt_ota_bank_offset(0) + 0x2000] ^= 0xFF;
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_CRC);
}

static void test_bit_flips(void) {
	host_flash_reset();
	flash_image(0, 0x4000, 7);
	CHECK(check_bank(0) == LT_OTA_IMAGE_OK);
	// one flipped bit anywhere in the body, both directions
	static const uint32_t at[] = {8, 0x3FF, LT_OTA_IMAGE_DESC_OFFSET + sizeof(lt_ota_image_desc_t), 0x2000, 0x3FFF};
	for (unsigned i = 0; i < sizeof(at) / sizeof(at[0]); i++) {
		for (uint8_t bit = 0; bit < 8; bit += 7) {
			flip_bit(0, at[i], bit);
			CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_CRC);
			flip_bit(0, at[i], bit);
		}
	}
	CHECK(check_bank(0) == LT_OTA_IMAGE_OK);
	// every bit of the descriptor
	for (uint32_t off = 0; off < sizeof(lt_ota_image_desc_t); off++) {
		for (uint8_t bit = 0; bit < 8; bit++) {
			flip_bit(0, LT_OTA_IMAGE_DESC_OFFSET + off, bit);
			CHECK(check_bank(0) != LT_OTA_IMAGE_OK);
			flip_bit(0, LT_OTA_IMAGE_DESC_OFFSET + off, bit);
		}
	}
	CHECK(check_bank(0) == LT_OTA_IMAGE_OK);
	// a flip in the CRC fields themselves is a mismatch, not a missing descriptor
	flip_bit(0, LT_OTA_IMAGE_CRC_OFFSET, 0);
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_CRC);
	flip_bit(0, LT_OTA_IMAGE_CRC_OFFSET, 0);
	flip_bit(0, LT_OTA_IMAGE_CRC_OFFSET + 4 + 1, 7); // crc_hi bit 15
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_CRC);
	flip_bit(0, LT_OTA_IMAGE_CRC_OFFSET + 4 + 1, 7);
	flip_bit(0, LT_OTA_IMAGE_CRC_OFFSET + 4 + 2, 0); // crc_hi bit 16: no longer a 16-bit half
	CHECK(check_bank(0) == LT_OTA_IMAGE_ERR_NO_DESC);
}

// The CRC skips exactly the two CRC words: changing them does not change it.
static void test_crc_span(void) {
	uint8_t a[0x800], b[0x800];
	image_bytes(a, sizeof(a), 9, 0);
	memcpy(b, a, sizeof(b));
	memset(b + LT_OTA_IMAGE_CRC_OFFSET, 0x5A, 8);
	CHECK(image_crc(a, sizeof(a)) == image_crc(b, sizeof(b)));
	b[LT_OTA_IMAGE_CRC_OFFSET - 1] ^= 1; // length, covered
	CHECK(image_crc(a, sizeof(a)) != image_crc(b, sizeof(b)));
	b[LT_OTA_IMAGE_CRC_OFFSET - 1] ^= 1;
	b[LT_OTA_IMAGE_CRC_OFFSET + 8] ^= 1; // first byte after the descriptor, covered
	CHECK(image_crc(a, sizeof(a)) != image_crc(b, sizeof(b)));
	CHECK(offsetof(lt_ota_image_desc_t, crc_lo) + LT_OTA_IMAGE_DESC_OFFSET == LT_OTA_IMAGE_CRC_OFFSET);
	CHECK(offsetof(lt_ota_image_desc_t, crc_hi) == offsetof(lt_ota_image_desc_t, crc_lo) + 4);
}

int main(void) {
	test_vectors();
	test_good();
	test_no_desc();
	test_wrong_bank();
	test_length();
	test_truncated();
	test_bit_flips();
	test_crc_span();
	return 0;
}
