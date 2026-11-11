/* Copyright (c) Andrew Rankin 2026-06-12. */

/*
 * Bank selection for the first-stage bootloader. Kept free of hardware access
 * so the host tests exercise exactly the code the bootloader runs.
 *
 * A bank "verifies" when lt_ota_image_check() accepts it: its own descriptor
 * (lt_ota_image.h) is present, says the image was linked for this bank, and
 * its length and CRC match the bank's contents.
 *
 * With metadata, a bank is "usable" when it verifies, its state is VALID,
 * TRIAL or CONFIRMED, and its descriptor CRC equals the image_crc recorded
 * with that state. A state belongs to the image it was recorded for, never to
 * the bank: an image the metadata does not know (a complete update whose
 * activation never got stored because of a power loss or a failed write, or
 * a bank reprogrammed directly) is treated like an EMPTY bank, so it is never
 * booted as known-good, never reverted to and never offered for rollback.
 *
 * Policy, with metadata:
 * - boot_bank is TRIAL with attempts left and is usable: use one attempt
 *   (stored before jumping) and boot it with the watchdog armed.
 * - boot_bank is TRIAL with no attempts left, or is not usable: revert to the
 *   other bank if that is usable, recording the failed trial as EMPTY so it
 *   is not offered for rollback again. With nothing to revert to, keep
 *   booting the trial image (watchdog armed, nothing stored).
 * - boot_bank is VALID/CONFIRMED (or EMPTY): boot it if it is usable, else
 *   the other bank if that is usable. Nothing is stored.
 * - boot_bank out of range in a CRC-valid record: boot the first usable bank.
 *   Nothing is stored.
 *
 * Without metadata (virgin device, meta erased by `pio run -t upload`, or a
 * record in an older format): boot the first bank that verifies, bank A
 * first, without the watchdog and without storing anything. So a single
 * image flashed over SWD boots once meta is erased, whichever bank it is in,
 * as long as the other bank does not verify; with both verifying, bank A
 * (what the factory image and `pio run -t upload` write) wins.
 *
 * Flashing a bank directly (debugger load, another SWD tool) with the
 * metadata left intact: the new image is unknown to the metadata, so the
 * other bank boots if it is usable; otherwise the new image boots through the
 * last resort below (boot_bank first). Erase the meta partition whenever a
 * bank is flashed directly.
 *
 * Last resort, when no bank is acceptable by the rules above: boot the first
 * bank whose vector table looks sane, preferring boot_bank (bank A without
 * metadata), with no verification, no watchdog and nothing stored. A device
 * running an unverified image can still be updated; one that refuses to boot
 * can only be recovered over SWD.
 */

#pragma once

#include "lt_ota_image.h"
#include "lt_ota_meta.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	// Bank verifies (lt_ota_image_check() == LT_OTA_IMAGE_OK); sets *crc to its descriptor CRC.
	bool (*image_ok)(void *arg, uint8_t bank, uint32_t *crc);
	// Bank's vector table looks bootable, nothing else known (lt_ota_vectors_ok()).
	bool (*vectors_ok)(void *arg, uint8_t bank);
	void *arg;
} lt_ota_boot_checks_t;

typedef struct {
	int bank;			// bank to boot, or -1 if nothing is bootable
	bool trial;			// booting a TRIAL image: arm the watchdog first
	bool store;			// meta must be written before booting
	lt_ota_meta_t meta; // updated metadata (when store is set)
} lt_ota_boot_t;

/**
 * Decide what to boot. meta is the newest valid record, or NULL if there is
 * none. Calls each check at most once per bank.
 */
void lt_ota_boot_select(const lt_ota_meta_t *meta, const lt_ota_boot_checks_t *chk, lt_ota_boot_t *out);

/**
 * Full bootloader decision against real flash: load the metadata, select,
 * store the update. If the store fails while consuming a trial attempt, the
 * attempt cannot be counted, so the other bank is booted instead when it
 * verifies (otherwise a crash-looping trial would never be reverted).
 *
 * @param trial set to whether the returned bank must be booted with the watchdog armed
 * @return bank to boot, or -1 if nothing is bootable
 */
int lt_ota_boot_run(const lt_ota_flash_ops_t *ops, bool *trial);

#ifdef __cplusplus
} // extern "C"
#endif
