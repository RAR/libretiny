/* Copyright (c) Andrew Rankin 2026-06-12. */

/*
 * Dual-bank OTA metadata shared by the first-stage bootloader, the application
 * (base/api/lt_ota.c) and the host tests.
 *
 * Flash layout comes from the board JSON "flash" map, which the builder turns
 * into FLASH_<PART>_OFFSET / FLASH_<PART>_LENGTH defines (builder/utils/flash.py):
 *   ota1  bank A (bank index 0), app linked at FLASH_OTA1_OFFSET
 *   ota2  bank B (bank index 1), app linked at FLASH_OTA2_OFFSET
 *   meta  two ping-pong metadata slots, one flash page each, at the start
 *
 * The metadata holds state: which bank boots next, and per bank whether its
 * image is known good, on trial or retired. Whether a bank holds a complete
 * image built for it comes from the image's own descriptor (lt_ota_image.h),
 * never from the metadata. Each state belongs to one image, identified by the
 * CRC-32 in that image's descriptor (image_crc): a bank rewritten by an update
 * or a programmer does not pass its state on to the new image (see
 * lt_ota_boot.h).
 *
 * Each metadata write goes to the slot that does not hold the newest record, so
 * a power failure between the page erase and the write leaves the previous
 * record intact. Records are CRC-protected; the newest valid one wins.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(FLASH_OTA1_OFFSET) || !defined(FLASH_OTA1_LENGTH) || !defined(FLASH_OTA2_OFFSET) || \
	!defined(FLASH_OTA2_LENGTH) || !defined(FLASH_META_OFFSET) || !defined(FLASH_META_LENGTH)
#error "lt_ota_meta.h needs the FLASH_OTA1/OTA2/META_OFFSET/_LENGTH defines generated from the board flash map"
#endif

// Record format 2. Format 1 ("OTA1", 40 bytes, with a length and CRC per bank
// computed on the device) is not read: such a record counts as no metadata.
#define LT_OTA_META_MAGIC	  0x4F544132u // "OTA2"
#define LT_OTA_BANK_COUNT	  2
#define LT_OTA_TRIAL_ATTEMPTS 3 // boots a TRIAL image gets before the bootloader reverts it

// One EFM32GG11 flash page (FLASH_PAGE_SIZE in the device header) per slot.
#define LT_OTA_META_SLOT_LEN  0x1000u
#define LT_OTA_META_SLOT0_OFF ((uint32_t)(FLASH_META_OFFSET))
#define LT_OTA_META_SLOT1_OFF (LT_OTA_META_SLOT0_OFF + LT_OTA_META_SLOT_LEN)

// SRAM window an image's initial stack pointer must fall into (SRAM_BASE/SRAM_SIZE).
#define LT_OTA_RAM_BASE 0x20000000u
#define LT_OTA_RAM_SIZE 0x00080000u

typedef enum {
	LT_OTA_BANK_EMPTY	  = 0, // not offered: never recorded, or a retired failed trial
	LT_OTA_BANK_VALID	  = 1, // known-good image (e.g. the SWD-flashed one)
	LT_OTA_BANK_TRIAL	  = 2, // newly activated, not yet confirmed
	LT_OTA_BANK_CONFIRMED = 3, // TRIAL image the application confirmed
} lt_ota_bank_state_t;

// On-flash record. The layout is part of the bootloader/application contract:
// do not reorder or resize without changing LT_OTA_META_MAGIC.
typedef struct {
	uint32_t magic;					  // LT_OTA_META_MAGIC
	uint32_t seq;					  // incremented on every write; compared with wraparound
	uint8_t boot_bank;				  // bank the bootloader boots: 0 = A, 1 = B
	uint8_t attempts;				  // remaining boots for a TRIAL boot_bank
	uint8_t state[LT_OTA_BANK_COUNT]; // lt_ota_bank_state_t per bank
	// Descriptor CRC-32 of the image each state was recorded for (unused while EMPTY)
	uint32_t image_crc[LT_OTA_BANK_COUNT];
	uint32_t crc32; // CRC-32 of all preceding bytes
} lt_ota_meta_t;

#ifndef __cplusplus
_Static_assert(sizeof(lt_ota_meta_t) == 24, "lt_ota_meta_t layout changed");
_Static_assert(sizeof(lt_ota_meta_t) % 4 == 0, "MSC writes whole words");
_Static_assert(LT_OTA_META_SLOT_LEN * 2 <= (FLASH_META_LENGTH), "meta partition too small for two slots");
#endif

/**
 * Flash access used by the shared code. Offsets are absolute flash addresses.
 * Each returns 0 on success and a negative value on error.
 */
typedef struct {
	int (*read)(uint32_t off, void *buf, uint32_t len);
	int (*erase)(uint32_t off, uint32_t len);				   // page-aligned
	int (*write)(uint32_t off, const void *buf, uint32_t len); // word-aligned
} lt_ota_flash_ops_t;

/** Base address of a bank (0 = A, 1 = B). */
uint32_t lt_ota_bank_offset(uint8_t bank);

/** Size of a bank in bytes, or 0 for an invalid index. */
uint32_t lt_ota_bank_length(uint8_t bank);

/** Bank containing the absolute flash address, or -1 if none. */
int lt_ota_bank_of(uint32_t addr);

/** Load the newest valid metadata record. Returns false if neither slot is valid. */
bool lt_ota_meta_load(const lt_ota_flash_ops_t *ops, lt_ota_meta_t *out);

/**
 * Write a new record into the slot not holding the newest one. Sets magic, seq
 * and crc32 in *m. Reads the record back and returns true only if it matches.
 */
bool lt_ota_meta_store(const lt_ota_flash_ops_t *ops, lt_ota_meta_t *m);

#ifdef __cplusplus
} // extern "C"
#endif
