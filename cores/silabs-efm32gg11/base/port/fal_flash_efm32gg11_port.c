/* Copyright (c) Andrew Rankin 2026-06-12. */

/*
 * FAL flash device for the EFM32GG11 internal flash (memory-mapped at 0,
 * 4 KiB pages), on emlib MSC. FlashDB, uf2ota staging and the OTA metadata
 * all go through it.
 *
 * Locking: emlib MSC is not reentrant, and FlashDB, OTA staging and
 * lt_ota_confirm() can run in different tasks, so every operation holds a
 * recursive mutex (lt_fal_lock(), also used by lt_ota.c to make metadata
 * read-modify-write sequences atomic). Before the scheduler starts there is
 * only one thread and the lock is a no-op.
 *
 * Interrupt latency: MSC_WriteWord() and MSC_ErasePage() leave interrupts
 * enabled (em_msc.c), and read-while-write is off (MSC_WRITECTRL_RWWEN reset
 * value, not changed by emlib). While a word write or a page erase is in
 * progress, any flash access stalls until it completes, including fetching an
 * interrupt handler or vector from flash. Interrupts are therefore delayed by
 * up to one page-erase time (see the device datasheet) during an erase.
 */

#include <libretiny.h>
#include <sdk_private.h>

#include "lt_ota_meta.h"

#include <FreeRTOS.h>
#include <em_msc.h>
#include <fal.h>
#include <semphr.h>
#include <string.h>
#include <task.h>

_Static_assert(LT_OTA_META_SLOT_LEN == FLASH_PAGE_SIZE, "metadata slot must be one flash page");

static SemaphoreHandle_t flash_mutex;

// Highest offset written in each OTA bank since the erase of its first page
// (0 = nothing staged), and whether lt_ota_staged() verified a completely
// written update in it, with no writes or erases in the bank since.
// lt_ota_switch() uses this only as "an update is staged"; the image length
// comes from the image's own descriptor.
static uint32_t ota_end[LT_OTA_BANK_COUNT];
static bool ota_complete[LT_OTA_BANK_COUNT];

static bool scheduler_running(void) {
	return xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
}

bool lt_fal_lock(void) {
	if (!scheduler_running())
		return true;
	if (!flash_mutex)
		return false; // creation failed in init(): refuse rather than run unprotected
	return xSemaphoreTakeRecursive(flash_mutex, portMAX_DELAY) == pdTRUE;
}

void lt_fal_unlock(void) {
	if (scheduler_running() && flash_mutex)
		xSemaphoreGiveRecursive(flash_mutex);
}

// Non-zero if a complete, verified update is staged in the bank (the number
// of bytes written to it), 0 otherwise.
uint32_t lt_fal_ota_written(uint8_t bank) {
	if (bank >= LT_OTA_BANK_COUNT || ota_end[bank] == 0 || !ota_complete[bank])
		return 0;
	return ota_end[bank] - lt_ota_bank_offset(bank);
}

void lt_fal_ota_set_complete(uint8_t bank) {
	if (bank < LT_OTA_BANK_COUNT && lt_fal_lock()) {
		ota_complete[bank] = ota_end[bank] != 0;
		lt_fal_unlock();
	}
}

static int init(void) {
	// called by fal_init() from lt_main(), before the scheduler starts
	MSC_Init();
	flash_mutex = xSemaphoreCreateRecursiveMutex();
	return flash_mutex ? 0 : -1;
}

static int read(long offset, uint8_t *buf, size_t size) {
	if (!lt_fal_lock())
		return -1;
	memcpy(buf, (const void *)(uintptr_t)offset, size);
	lt_fal_unlock();
	return (int)size;
}

static int write_locked(long offset, const uint8_t *buf, size_t size) {
	// MSC writes whole words at word-aligned addresses: a trailing partial
	// word is padded with 0xFF, which leaves those bytes unprogrammed.
	if (offset & 0x3)
		return -1;
	uint8_t tmp[256];
	for (size_t done = 0; done < size;) {
		size_t n = size - done < sizeof(tmp) ? size - done : sizeof(tmp);
		memcpy(tmp, buf + done, n);
		size_t words = (n + 3u) & ~(size_t)3u;
		memset(tmp + n, 0xFF, words - n);
		if (MSC_WriteWord((uint32_t *)(uintptr_t)(offset + (long)done), tmp, words) != mscReturnOk)
			return -1;
		done += n;
	}
	int bank = lt_ota_bank_of((uint32_t)offset);
	if (bank >= 0) {
		ota_complete[bank] = false;
		if ((uint32_t)offset + size > ota_end[bank])
			ota_end[bank] = (uint32_t)offset + size;
	}
	return (int)size;
}

static int write(long offset, const uint8_t *buf, size_t size) {
	if (!lt_fal_lock())
		return -1;
	int ret = write_locked(offset, buf, size);
	lt_fal_unlock();
	return ret;
}

static int erase_locked(long offset, size_t size) {
	long start = offset & ~((long)FLASH_PAGE_SIZE - 1);
	long end   = (offset + (long)size + (long)FLASH_PAGE_SIZE - 1) & ~((long)FLASH_PAGE_SIZE - 1);
	for (long addr = start; addr < end; addr += FLASH_PAGE_SIZE) {
		if (MSC_ErasePage((uint32_t *)(uintptr_t)addr) != mscReturnOk)
			return -1;
	}
	for (uint8_t bank = 0; bank < LT_OTA_BANK_COUNT; bank++) {
		long base = (long)lt_ota_bank_offset(bank);
		if (start <= base && base < end)
			ota_end[bank] = 0;
		if (start < base + (long)lt_ota_bank_length(bank) && base < end)
			ota_complete[bank] = false;
	}
	// Report the erased span from offset, not from the page start: uf2ota skips
	// erasing blocks that fall inside [offset, offset + returned length), so an
	// understated length makes it erase the page again over blocks it has
	// already written.
	return (int)(end - offset);
}

static int erase(long offset, size_t size) {
	if (!lt_fal_lock())
		return -1;
	int ret = erase_locked(offset, size);
	lt_fal_unlock();
	return ret;
}

const struct fal_flash_dev flash0 = {
	.name		= FAL_FLASH_DEV_NAME,
	.addr		= 0x0,
	.len		= FLASH_LENGTH,
	.blk_size	= FLASH_PAGE_SIZE,
	.ops		= {init, read, write, erase},
	.write_gran = 32, // bits; matches FDB_WRITE_GRAN set by the family builder
};
