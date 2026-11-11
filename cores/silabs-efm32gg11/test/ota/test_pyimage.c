/* Copyright (c) Andrew Rankin 2026-06-12. */

/*
 * C half of the check that builder/utils/efm32gg11_image.py and lt_ota_image.c
 * agree (driven by test_pyimage.py):
 *   test_pyimage gen DIR    write, per bank, raw_<n>.bin (descriptor as linked,
 *                           CRC words 0xFFFFFFFF) and c_<n>.bin (filled in here),
 *                           and manifest.txt ("<n> <link address>" per bank)
 *   test_pyimage check DIR  load py_<n>.bin (raw_<n>.bin filled in by the
 *                           Python tool) into its bank and verify it
 */

#include "host_flash.h"
#include "lt_ota_image.h"
#include "test_image.h"

#include <stdio.h>
#include <string.h>

// Odd lengths on purpose: the binary of a real image need not be word-sized.
static const uint32_t img_len[LT_OTA_BANK_COUNT] = {0x1A2B5, 0x0C001};

static void path(char *buf, size_t size, const char *dir, const char *name, unsigned bank) {
	snprintf(buf, size, "%s/%s_%u.bin", dir, name, bank);
}

static void write_file(const char *p, const uint8_t *data, uint32_t len) {
	FILE *f = fopen(p, "wb");
	CHECK(f && fwrite(data, 1, len, f) == len && fclose(f) == 0);
}

static uint32_t read_file(const char *p, uint8_t *data, uint32_t max) {
	FILE *f = fopen(p, "rb");
	CHECK(f);
	uint32_t len = (uint32_t)fread(data, 1, max, f);
	CHECK(fclose(f) == 0);
	return len;
}

static int gen(const char *dir) {
	static uint8_t img[0x100000];
	char p[512];
	snprintf(p, sizeof(p), "%s/manifest.txt", dir);
	FILE *manifest = fopen(p, "w");
	CHECK(manifest);
	for (uint8_t bank = 0; bank < LT_OTA_BANK_COUNT; bank++) {
		uint32_t len = img_len[bank];
		image_bytes(img, len, 100 + bank, bank);
		path(p, sizeof(p), dir, "c", bank);
		write_file(p, img, len);
		// as the linker leaves it (base/api/lt_ota.c)
		memset(img + LT_OTA_IMAGE_CRC_OFFSET, 0xFF, 8);
		path(p, sizeof(p), dir, "raw", bank);
		write_file(p, img, len);
		fprintf(manifest, "%u 0x%06X\n", bank, (unsigned)lt_ota_bank_offset(bank));
	}
	CHECK(fclose(manifest) == 0);
	return 0;
}

static int check(const char *dir) {
	char p[512];
	host_flash_reset();
	for (uint8_t bank = 0; bank < LT_OTA_BANK_COUNT; bank++) {
		uint8_t *mem = host_flash_mem() + lt_ota_bank_offset(bank);
		path(p, sizeof(p), dir, "py", bank);
		uint32_t len = read_file(p, mem, lt_ota_bank_length(bank));
		CHECK(len == img_len[bank]);
		// the Python CRC equals this side's
		uint32_t got = 0;
		CHECK(lt_ota_image_check(&host_flash_ops, bank, &got) == LT_OTA_IMAGE_OK && got == image_crc(mem, len));
		const lt_ota_image_desc_t *d = (const lt_ota_image_desc_t *)(mem + LT_OTA_IMAGE_DESC_OFFSET);
		CHECK(d->length == len && (d->crc_hi << 16 | d->crc_lo) == got);
	}
	// each image is rejected in the other bank
	static uint8_t a[0x100000];
	memcpy(a, host_flash_mem() + lt_ota_bank_offset(0), img_len[0]);
	memset(host_flash_mem() + lt_ota_bank_offset(1), 0xFF, lt_ota_bank_length(1));
	memcpy(host_flash_mem() + lt_ota_bank_offset(1), a, img_len[0]);
	CHECK(lt_ota_image_check(&host_flash_ops, 1, NULL) == LT_OTA_IMAGE_ERR_WRONG_BANK);
	return 0;
}

int main(int argc, char **argv) {
	if (argc == 3 && strcmp(argv[1], "gen") == 0)
		return gen(argv[2]);
	if (argc == 3 && strcmp(argv[1], "check") == 0)
		return check(argv[2]);
	fprintf(stderr, "usage: %s gen|check DIR\n", argv[0]);
	return 2;
}
