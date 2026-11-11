/* Copyright (c) Andrew Rankin 2026-06-12. */

/*
 * Application image descriptor, shared by the bootloader, the application
 * (base/api/lt_ota.c) and the host tests.
 *
 * Every application image carries one lt_ota_image_desc_t at
 * LT_OTA_IMAGE_DESC_OFFSET from its start, right after the vector table
 * (misc/efm32gg11b820_sections.ld places the .lt_image_desc section there).
 * The application defines it with magic, version, link_addr and length set
 * at link time and the CRC halves left at 0xFFFFFFFF; after linking, the
 * family builder (builder/utils/efm32gg11_image.py) checks those fields
 * against the linked binary and fills in the CRC, in the ELF itself, so that
 * every image made from it (bank images, factory image, UF2) and a debugger
 * load all carry the filled-in descriptor.
 *
 * The CRC is the reflected IEEE CRC-32 (lt_crc32(), zlib.crc32()) of the
 * image bytes [0, length) with the 8 bytes of crc_lo/crc_hi left out. It is
 * split into two 16-bit halves, each in its own word, because the UF2 packer
 * stores bank B as a "diff32" patch over bank A: each differing word is
 * encoded as a signed 32-bit difference, so a word whose bank A and bank B
 * values are more than 2^31 apart (a quarter of all CRC pairs) cannot be
 * packed. Two words below 0x10000 always can.
 *
 * Layout rules: the fields below never change meaning. A later version may
 * only append fields (the CRC covers them) and raises version; readers accept
 * any version >= LT_OTA_IMAGE_VERSION. Any other change needs a new magic.
 */

#pragma once

#include "lt_ota_meta.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LT_OTA_IMAGE_DESC_OFFSET 0x400u		 // from the image start; the vector table is 0x3B8 bytes
#define LT_OTA_IMAGE_MAGIC		 0x4D49544Cu // "LTIM" in flash byte order
#define LT_OTA_IMAGE_VERSION	 1u

typedef struct {
	uint32_t magic;		// LT_OTA_IMAGE_MAGIC
	uint32_t version;	// LT_OTA_IMAGE_VERSION
	uint32_t link_addr; // flash address the image is linked at: its bank's base
	uint32_t length;	// image length in bytes from link_addr, descriptor included
	uint32_t crc_lo;	// bits 15..0 of the image CRC-32; the upper half of the word is 0
	uint32_t crc_hi;	// bits 31..16 of the image CRC-32, in the lower half; the upper half is 0
} lt_ota_image_desc_t;

#ifndef __cplusplus
_Static_assert(sizeof(lt_ota_image_desc_t) == 24, "lt_ota_image_desc_t layout changed");
_Static_assert(LT_OTA_IMAGE_DESC_OFFSET % 4 == 0, "descriptor must be word-aligned");
#endif

// Offset of crc_lo from the image start; the CRC skips [CRC_OFFSET, CRC_OFFSET + 8).
#define LT_OTA_IMAGE_CRC_OFFSET (LT_OTA_IMAGE_DESC_OFFSET + 16u)

typedef enum {
	LT_OTA_IMAGE_OK = 0,
	LT_OTA_IMAGE_ERR_READ,		 // flash read failed
	LT_OTA_IMAGE_ERR_NO_DESC,	 // no descriptor: erased, foreign or not filled in by the builder
	LT_OTA_IMAGE_ERR_WRONG_BANK, // linked for the other bank
	LT_OTA_IMAGE_ERR_LENGTH,	 // length too short for the descriptor or too long for the bank
	LT_OTA_IMAGE_ERR_VECTORS,	 // vector table not bootable (lt_ota_vectors_ok())
	LT_OTA_IMAGE_ERR_CRC,		 // contents differ from what was built (truncated or corrupt)
} lt_ota_image_err_t;

/**
 * Verify the image in a bank against its own descriptor: descriptor present,
 * linked for this bank, length within the bank, vector table sane, CRC
 * matches. Costs one CRC pass over the image.
 * @param crc set to the image CRC-32 on success (may be NULL); the metadata
 * uses it to tell one image from another (lt_ota_meta_t.image_crc)
 */
lt_ota_image_err_t lt_ota_image_check(const lt_ota_flash_ops_t *ops, uint8_t bank, uint32_t *crc);

/**
 * Sanity-check the vector table of an image of len bytes in a bank: the
 * initial stack pointer is a word-aligned address in SRAM (or its end) and the
 * reset vector is a Thumb address inside the image. Catches an empty bank, an
 * image linked for the other bank and gross truncation; it does not prove the
 * image is complete (lt_ota_image_check() does that).
 */
bool lt_ota_vectors_ok(const lt_ota_flash_ops_t *ops, uint8_t bank, uint32_t len);

#ifdef __cplusplus
} // extern "C"
#endif
