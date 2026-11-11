/* Copyright (c) Andrew Rankin 2026-06-12. */

/*
 * Application side of the dual-bank OTA scheme: the metadata transitions behind
 * lt_ota_switch(), lt_ota_confirm(), lt_ota_is_valid() and
 * lt_ota_dual_get_stored(). base/api/lt_ota.c supplies the hardware facts
 * (running bank, staged update) and serialises the calls.
 *
 * Images are verified against their own descriptors (lt_ota_image.h); the
 * metadata records a state per bank together with the descriptor CRC of the
 * image it belongs to (lt_ota_meta.h, lt_ota_boot.h).
 *
 * Every switch records the running image as well, so that after the first OTA
 * following an SWD flash (when there is no metadata) the bootloader has an
 * image to revert to: if the running image verifies, the running bank becomes
 * VALID for it (an existing state is kept if it belongs to this image). If it
 * does not verify (it was started by the bootloader's last resort, e.g. after
 * a flash bit error), it is recorded EMPTY instead. The update is still
 * activated, since it may be the repair, but there is then nothing to revert
 * to: a trial that never confirms keeps booting with the watchdog armed
 * (lt_ota_boot.h), and a revert (switch(true)) also retires the cancelled
 * update, which the bootloader would otherwise boot instead.
 */

#pragma once

#include "lt_ota_image.h"
#include "lt_ota_meta.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	const lt_ota_flash_ops_t *ops;
	uint8_t running; // bank the application executes from
	// A complete update was written into the other bank during this boot and
	// verified by lt_ota_image_check() when it finished (lt_ota_staged()), with
	// no writes to that bank since.
	bool staged;
} lt_ota_app_t;

/** Bank (0/1) the bootloader is set to boot next. */
uint8_t lt_ota_app_stored(const lt_ota_app_t *app);

/**
 * The running bank is valid; the other one is valid if it is usable: the
 * metadata offers it (not EMPTY) for the image it holds, and that image verifies.
 */
bool lt_ota_app_is_valid(const lt_ota_app_t *app, uint8_t bank);

/**
 * lt_ota_switch() semantics:
 * - revert = true: if the other bank is set to boot next, point boot back at
 *   the running bank. A no-op returning true when the running bank already boots next.
 * - revert = false: activate the other bank as a TRIAL image. Its image must
 *   verify. If nothing was staged during this boot (Update.rollBack(), or a
 *   switch after an aborted update), it must also be usable: offered (not
 *   EMPTY) and the image its state was recorded for. When the other bank
 *   already boots next and nothing new was staged, it is only verified and
 *   nothing is written.
 * @return false if the target image is not valid or the metadata write failed
 */
bool lt_ota_app_switch(const lt_ota_app_t *app, bool revert);

/**
 * Commit the running image if it is the TRIAL image the bootloader is booting.
 * Writes flash only in that case.
 * @return false only if a needed metadata write failed
 */
bool lt_ota_app_confirm(const lt_ota_app_t *app);

#ifdef __cplusplus
} // extern "C"
#endif
