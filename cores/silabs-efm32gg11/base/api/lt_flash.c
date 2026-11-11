/* Copyright (c) Andrew Rankin 2026-06-12. */

#include <libretiny.h>
#include <sdk_private.h>

/*
 * Internal flash only, so there is no JEDEC ID. The size code makes the common
 * lt_flash_get_size() report the size; reads, writes and erases go through FAL
 * (base/port/fal_flash_efm32gg11_port.c).
 */
lt_flash_id_t lt_flash_get_id(void) {
	_Static_assert(FLASH_SIZE == (1u << 21), "chip_size_id below assumes 2 MiB");
	lt_flash_id_t id = {
		.manufacturer_id = 0x00,
		.chip_id		 = 0x00,
		.chip_size_id	 = 21, // log2(FLASH_SIZE)
	};
	return id;
}
