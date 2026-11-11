/* Copyright (c) Andrew Rankin 2026-06-12. */

/*
 * Dual-bank OTA for silabs-efm32gg11. The image is linked once per bank
 * (FLASH_OTA1_OFFSET, FLASH_OTA2_OFFSET) and carries its own descriptor
 * (base/ota/lt_ota_image.h, defined below); the first-stage bootloader picks
 * the bank from the state in the meta partition and verifies it against that
 * descriptor. The metadata transitions live in base/ota/lt_ota_app.c, which
 * is shared with the host tests; this file supplies the running bank, the
 * staging state and the locking.
 *
 * cores/common/base/api/lt_ota.c provides the rest of the API (begin, write,
 * end, can_rollback, get_uf2_scheme).
 */

#include <libretiny.h>
#include <sdk_private.h>

#include "lt_ota_app.h"
#include "lt_ota_image.h"
#include "lt_ota_meta.h"

#include <fal.h>

_Static_assert(LT_OTA_RAM_BASE == SRAM_BASE && LT_OTA_RAM_SIZE == SRAM_SIZE, "SRAM window mismatch");

// Start (vector table, vendor startup) and length (linker script) of this image.
extern const uint32_t __Vectors[];
extern char _lt_image_size[];
// FAL port (base/port/fal_flash_efm32gg11_port.c)
extern bool lt_fal_lock(void);
extern void lt_fal_unlock(void);
extern uint32_t lt_fal_ota_written(uint8_t bank);
extern void lt_fal_ota_set_complete(uint8_t bank);
// lt_wdt.c
extern void lt_wdt_release_boot(void);

static int ota_read(uint32_t off, void *buf, uint32_t len) {
	const struct fal_flash_dev *dev = fal_flash_device_find(FAL_FLASH_DEV_NAME);
	return dev && dev->ops.read((long)off, buf, len) >= 0 ? 0 : -1;
}

static int ota_erase(uint32_t off, uint32_t len) {
	const struct fal_flash_dev *dev = fal_flash_device_find(FAL_FLASH_DEV_NAME);
	return dev && dev->ops.erase((long)off, len) >= 0 ? 0 : -1;
}

static int ota_write(uint32_t off, const void *buf, uint32_t len) {
	const struct fal_flash_dev *dev = fal_flash_device_find(FAL_FLASH_DEV_NAME);
	return dev && dev->ops.write((long)off, buf, len) >= 0 ? 0 : -1;
}

static const lt_ota_flash_ops_t ota_flash_ops = {ota_read, ota_erase, ota_write};

// This image's descriptor. The linker script places .lt_image_desc at
// LT_OTA_IMAGE_DESC_OFFSET; the family builder checks magic, version,
// link_addr and length against the linked binary and replaces the CRC
// placeholders (builder/utils/efm32gg11_image.py).
__attribute__((used, section(".lt_image_desc"))) const lt_ota_image_desc_t lt_ota_image_desc = {
	.magic	   = LT_OTA_IMAGE_MAGIC,
	.version   = LT_OTA_IMAGE_VERSION,
	.link_addr = (uint32_t)(uintptr_t)__Vectors,
	.length	   = (uint32_t)(uintptr_t)_lt_image_size,
	.crc_lo	   = 0xFFFFFFFFu,
	.crc_hi	   = 0xFFFFFFFFu,
};

static const char *image_err_str(lt_ota_image_err_t err) {
	switch (err) {
		case LT_OTA_IMAGE_OK:
			return "ok";
		case LT_OTA_IMAGE_ERR_READ:
			return "flash read failed";
		case LT_OTA_IMAGE_ERR_NO_DESC:
			return "no image descriptor";
		case LT_OTA_IMAGE_ERR_WRONG_BANK:
			return "image built for the other bank";
		case LT_OTA_IMAGE_ERR_LENGTH:
			return "image length out of range";
		case LT_OTA_IMAGE_ERR_VECTORS:
			return "vector table not bootable";
		case LT_OTA_IMAGE_ERR_CRC:
			return "CRC mismatch (truncated or corrupt)";
	}
	return "unknown error";
}

static lt_ota_app_t ota_app(void) {
	// SystemInit points VTOR at the vector table of the running image
	uint8_t running = lt_ota_bank_of(SCB->VTOR) == 1 ? 1 : 0;
	return (lt_ota_app_t){
		.ops	 = &ota_flash_ops,
		.running = running,
		.staged	 = lt_fal_ota_written(running ^ 1u) != 0,
	};
}

lt_ota_type_t lt_ota_get_type(void) {
	return OTA_TYPE_DUAL;
}

uint8_t lt_ota_dual_get_current(void) {
	return ota_app().running + 1;
}

uint8_t lt_ota_dual_get_stored(void) {
	if (!lt_fal_lock())
		return lt_ota_dual_get_current();
	lt_ota_app_t app = ota_app();
	uint8_t stored	 = lt_ota_app_stored(&app);
	lt_fal_unlock();
	return stored + 1;
}

bool lt_ota_is_valid(uint8_t index) {
	if (index < 1 || index > LT_OTA_BANK_COUNT || !lt_fal_lock())
		return false;
	lt_ota_app_t app = ota_app();
	bool valid		 = lt_ota_app_is_valid(&app, index - 1);
	lt_fal_unlock();
	return valid;
}

bool lt_ota_switch(bool revert) {
	if (!lt_fal_lock())
		return false;
	lt_ota_app_t app = ota_app();
	bool ok			 = lt_ota_app_switch(&app, revert);
	lt_fal_unlock();
	return ok;
}

void lt_ota_staged(lt_ota_ctx_t *ctx) {
	(void)ctx;
	// uf2ota writes the update into the bank that is not running. Arm it only
	// if it is the image that was built for that bank; lt_ota_switch(), called
	// next by lt_ota_end(), then refuses to activate anything else.
	uint8_t bank = ota_app().running ^ 1u;
	if (!lt_fal_lock()) {
		LT_EM(OTA, "Cannot verify the update: flash unavailable");
		return;
	}
	lt_ota_image_err_t err = lt_ota_image_check(&ota_flash_ops, bank, NULL);
	if (err == LT_OTA_IMAGE_OK)
		lt_fal_ota_set_complete(bank);
	lt_fal_unlock();
	if (err != LT_OTA_IMAGE_OK)
		LT_EM(OTA, "Update rejected: %s", image_err_str(err));
}

void lt_ota_confirm(void) {
	if (!lt_fal_lock()) {
		LT_EM(OTA, "Cannot confirm the running image: flash unavailable");
		return;
	}
	lt_ota_app_t app = ota_app();
	bool ok			 = lt_ota_app_confirm(&app);
	lt_fal_unlock();
	if (!ok) {
		// still a TRIAL image: keep the bootloader's watchdog, so that the
		// attempt count keeps protecting the device
		LT_EM(OTA, "Confirming the running image failed");
		return;
	}
	// the bootloader arms the watchdog before starting a TRIAL image
	lt_wdt_release_boot();
}
