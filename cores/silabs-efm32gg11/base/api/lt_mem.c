/* Copyright (c) Andrew Rankin 2026-06-12. */

#include <libretiny.h>
#include <sdk_private.h>

uint32_t lt_ram_get_size(void) {
	return SRAM_SIZE; // 512 KiB on the EFM32GG11B820 (device header)
}
