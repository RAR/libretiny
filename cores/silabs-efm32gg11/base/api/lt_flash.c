/* Copyright (c) WGM160P-LibreTiny port 2026-05-26.
 *
 * Phase 1 flash API stub for silabs-efm32gg11.
 * Phase 2's OTA path will fill these in by wrapping emlib MSC_*.
 *
 * NOTE: cores/common/base/api/lt_flash.c provides weak defaults for
 * lt_flash_get_size, lt_flash_erase, lt_flash_erase_block, lt_flash_read,
 * lt_flash_write — but they all use fal_*() (FlashDB's Flash Abstraction
 * Layer), which isn't wired in Phase 1. The family provides lt_flash_get_id
 * (the only non-weak symbol the common API requires) and overrides the
 * read/write/erase paths with stubs so link succeeds even before fal
 * lands.
 *
 * Signatures match cores/common/base/api/lt_flash.h (uint32_t returns
 * for read/write, not bool as the plan guessed).
 */

#include "lt_family.h"
#include <libretiny.h>

lt_flash_id_t lt_flash_get_id(void) {
    // EFM32GG11B has internal flash only (no external SPI flash).
    // Return a synthetic identifier so callers querying chip ID get
    // something stable. Manufacturer 0x00 (no JEDEC ID for internal flash).
    lt_flash_id_t id = {
        .manufacturer_id = 0x00,
        .chip_id         = 0x00,
        .chip_size_id    = 0x15,   // 2^21 = 2 MiB (matches EFM32GG11B820 flash size)
    };
    return id;
}

uint32_t lt_flash_get_size(void) {
    return 2 * 1024 * 1024;  // 2 MiB internal flash on EFM32GG11B820
}

bool lt_flash_erase(uint32_t offset, size_t length) {
    (void)offset;
    (void)length;
    return false;  // not implemented in Phase 1
}

bool lt_flash_erase_block(uint32_t offset) {
    (void)offset;
    return false;  // not implemented in Phase 1
}

uint32_t lt_flash_read(uint32_t offset, uint8_t *data, size_t length) {
    (void)offset;
    (void)data;
    (void)length;
    return 0;  // not implemented in Phase 1
}

uint32_t lt_flash_write(uint32_t offset, const uint8_t *data, size_t length) {
    (void)offset;
    (void)data;
    (void)length;
    return 0;  // not implemented in Phase 1
}
