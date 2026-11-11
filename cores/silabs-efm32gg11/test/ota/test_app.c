/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "host_flash.h"
#include "lt_ota_app.h"
#include "lt_ota_boot.h"
#include "test_image.h"

#include <string.h>

static const lt_ota_flash_ops_t *ops = &host_flash_ops;

#define IMG_LEN 0x4000u

static lt_ota_app_t app_on(uint8_t running, bool staged) {
	return (lt_ota_app_t){.ops = ops, .running = running, .staged = staged};
}

// What lt_ota_end() leads to in base/api/lt_ota.c once an update has been
// written completely into the other bank: lt_ota_staged() marks it staged only
// if it verifies, then lt_ota_switch(false) runs.
static lt_ota_app_t after_update(uint8_t running) {
	return app_on(running, check_bank(running ^ 1u) == LT_OTA_IMAGE_OK);
}

static lt_ota_meta_t load(void) {
	lt_ota_meta_t m;
	CHECK(lt_ota_meta_load(ops, &m));
	return m;
}

static bool have_meta(void) {
	lt_ota_meta_t m;
	return lt_ota_meta_load(ops, &m);
}

static int boot(bool *trial) {
	return lt_ota_boot_run(ops, trial);
}

// SWD flash of bank A with the metadata erased, then an OTA staged into B.
static void fresh_device_with_staged_b(lt_ota_app_t *app) {
	host_flash_reset();
	flash_image(0, IMG_LEN, 1);
	stage_image(1, 0x5001, 2, 1);
	*app = after_update(0);
	CHECK(app->staged);
}

static void test_first_ota_crash_loop(void) {
	lt_ota_app_t app;
	bool trial;
	fresh_device_with_staged_b(&app);
	CHECK(lt_ota_app_stored(&app) == 0);
	CHECK(!lt_ota_app_is_valid(&app, 1)); // staged but not activated

	CHECK(lt_ota_app_switch(&app, false));
	lt_ota_meta_t m = load();
	CHECK(m.boot_bank == 1 && m.attempts == LT_OTA_TRIAL_ATTEMPTS);
	// the running bank got a state, so there is something to revert to
	CHECK(m.state[0] == LT_OTA_BANK_VALID && m.state[1] == LT_OTA_BANK_TRIAL);
	CHECK(lt_ota_app_stored(&app) == 1);

	// the new image never confirms: three trial boots, then back to A
	for (int i = 0; i < LT_OTA_TRIAL_ATTEMPTS; i++)
		CHECK(boot(&trial) == 1 && trial);
	CHECK(boot(&trial) == 0 && !trial);
	CHECK(boot(&trial) == 0 && !trial);
	lt_ota_app_t back = app_on(0, false);
	CHECK(lt_ota_app_stored(&back) == 0);
	CHECK(!lt_ota_app_is_valid(&back, 1)); // the failed image is not offered for rollback
	CHECK(!lt_ota_app_switch(&back, false));
	// the same image downloaded again is a new update and may be tried again
	stage_image(1, 0x5001, 2, 1);
	CHECK(lt_ota_app_switch(&(lt_ota_app_t){.ops = ops, .running = 0, .staged = true}, false));
	CHECK(boot(&trial) == 1 && trial);
}

static void test_confirm(void) {
	lt_ota_app_t app;
	bool trial;
	fresh_device_with_staged_b(&app);
	CHECK(lt_ota_app_switch(&app, false));
	CHECK(boot(&trial) == 1 && trial);

	lt_ota_app_t on_b = app_on(1, false);
	unsigned writes	  = host_write_count;
	CHECK(lt_ota_app_confirm(&on_b));
	CHECK(host_write_count == writes + 1);
	lt_ota_meta_t m = load();
	CHECK(m.boot_bank == 1 && m.attempts == 0 && m.state[1] == LT_OTA_BANK_CONFIRMED);
	CHECK(m.state[0] == LT_OTA_BANK_VALID);

	// idempotent and free once confirmed
	writes = host_write_count;
	CHECK(lt_ota_app_confirm(&on_b));
	CHECK(lt_ota_app_confirm(&on_b));
	CHECK(boot(&trial) == 1 && !trial);
	CHECK(host_write_count == writes);

	// confirm with no metadata at all writes nothing
	host_flash_reset();
	flash_image(0, IMG_LEN, 1);
	lt_ota_app_t swd = app_on(0, false);
	CHECK(lt_ota_app_confirm(&swd));
	CHECK(host_write_count == 0 && host_erase_count == 0);

	// a confirm write that fails is reported, and the trial continues
	fresh_device_with_staged_b(&app);
	CHECK(lt_ota_app_switch(&app, false));
	CHECK(boot(&trial) == 1 && trial);
	host_fail_write_at = 1;
	CHECK(!lt_ota_app_confirm(&on_b));
	m = load();
	CHECK(m.state[1] == LT_OTA_BANK_TRIAL && m.attempts == LT_OTA_TRIAL_ATTEMPTS - 1);
}

static void test_revert_before_reboot(void) {
	lt_ota_app_t app;
	bool trial;
	fresh_device_with_staged_b(&app);

	// revert with nothing switched is a no-op
	CHECK(lt_ota_app_switch(&app, true));
	CHECK(host_write_count == 1); // only the staging write

	CHECK(lt_ota_app_switch(&app, false));
	// switching again with nothing new staged is a no-op
	lt_ota_app_t after = app_on(0, false);
	unsigned writes	   = host_write_count;
	CHECK(lt_ota_app_switch(&after, false));
	CHECK(host_write_count == writes);

	CHECK(lt_ota_app_switch(&app, true));
	CHECK(lt_ota_app_stored(&app) == 0);
	lt_ota_meta_t m = load();
	CHECK(m.boot_bank == 0 && m.attempts == 0 && m.state[0] == LT_OTA_BANK_VALID);
	writes = host_write_count;
	CHECK(lt_ota_app_switch(&app, true));
	CHECK(host_write_count == writes);
	CHECK(boot(&trial) == 0 && !trial);

	// the staged image is still valid, so it can be activated again
	CHECK(lt_ota_app_is_valid(&after, 1));
	CHECK(lt_ota_app_switch(&after, false));
	CHECK(boot(&trial) == 1 && trial);
}

static void test_rollback_after_reboot(void) {
	lt_ota_app_t app;
	bool trial;
	fresh_device_with_staged_b(&app);
	CHECK(lt_ota_app_switch(&app, false));
	CHECK(boot(&trial) == 1 && trial);
	lt_ota_app_t on_b = app_on(1, false);
	CHECK(lt_ota_app_confirm(&on_b));

	// Update.rollBack(): canRollback() is true, switch(false) uses the stored state
	CHECK(lt_ota_app_is_valid(&on_b, 0));
	CHECK(lt_ota_app_is_valid(&on_b, 1)); // the running bank
	CHECK(lt_ota_app_switch(&on_b, false));
	lt_ota_meta_t m = load();
	CHECK(m.boot_bank == 0 && m.state[0] == LT_OTA_BANK_TRIAL && m.state[1] == LT_OTA_BANK_CONFIRMED);
	CHECK(boot(&trial) == 0 && trial);
	lt_ota_app_t on_a = app_on(0, false);
	CHECK(lt_ota_app_confirm(&on_a));
	CHECK(boot(&trial) == 0 && !trial);

	// rollback to a bank whose image was overwritten (aborted OTA) is refused
	stage_partial(1, 0x6000, 0x800, 9, 1);
	CHECK(!lt_ota_app_is_valid(&on_a, 1));
	unsigned writes = host_write_count;
	CHECK(!lt_ota_app_switch(&on_a, false));
	CHECK(host_write_count == writes);
}

static void test_bad_staged_images(void) {
	bool trial;
	lt_ota_app_t app;

	// Each complete download fails lt_ota_staged()'s check, so the switch that
	// lt_ota_end() makes next refuses it, and nothing is written.
	for (int kind = 0; kind < 6; kind++) {
		host_flash_reset();
		flash_image(0, IMG_LEN, 1);
		switch (kind) {
			case 0: // linked for the wrong bank
				stage_image(1, 0x5000, 2, 0);
				break;
			case 1: // too short to hold a descriptor
				stage_image(1, 0x80, 2, 1);
				break;
			case 2: // truncated: the end of the image is missing
				stage_image(1, 0x5000, 2, 1);
				memset(host_flash_mem() + lt_ota_bank_offset(1) + 0x4000, 0xFF, 0x1000);
				break;
			case 3: // one bit flipped in the body during the download
				stage_image(1, 0x5000, 2, 1);
				flip_bit(1, 0x1234, 6);
				break;
			case 4: // one bit flipped in the descriptor
				stage_image(1, 0x5000, 2, 1);
				flip_bit(1, LT_OTA_IMAGE_DESC_OFFSET + 8, 4);
				break;
			case 5: // built without a filled-in descriptor
				stage_image(1, 0x5000, 2, 1);
				memset(host_flash_mem() + lt_ota_bank_offset(1) + LT_OTA_IMAGE_CRC_OFFSET, 0xFF, 8);
				break;
		}
		app = after_update(0);
		CHECK(!app.staged);
		CHECK(!lt_ota_app_switch(&app, false));
		CHECK(!have_meta());
		// even if lt_ota_staged() were bypassed, the switch verifies the bank itself
		app.staged = true;
		CHECK(!lt_ota_app_switch(&app, false));
		CHECK(!have_meta());
		CHECK(boot(&trial) == 0 && !trial);
	}

	// nothing staged and no state recorded
	host_flash_reset();
	flash_image(0, IMG_LEN, 1);
	flash_image(1, IMG_LEN, 2); // verifies, but the metadata does not offer it
	app = app_on(0, false);
	CHECK(!lt_ota_app_is_valid(&app, 1));
	CHECK(!lt_ota_app_switch(&app, false));
	CHECK(!have_meta());
}

static void test_bad_download_over_known_bank(void) {
	// B holds a confirmed earlier image (rolled back from), then a corrupt
	// download overwrites it: refused, and B is no longer offered
	lt_ota_app_t app;
	bool trial;
	fresh_device_with_staged_b(&app);
	CHECK(lt_ota_app_switch(&app, false));
	CHECK(boot(&trial) == 1 && trial);
	lt_ota_app_t on_b = app_on(1, false);
	CHECK(lt_ota_app_confirm(&on_b));
	CHECK(lt_ota_app_switch(&on_b, false)); // Update.rollBack() to A
	CHECK(boot(&trial) == 0 && trial);
	lt_ota_app_t on_a = app_on(0, false);
	CHECK(lt_ota_app_confirm(&on_a));
	CHECK(load().state[1] == LT_OTA_BANK_CONFIRMED);

	stage_image(1, 0x6000, 5, 1);
	flip_bit(1, 0x5FFF, 0);
	app = after_update(0);
	CHECK(!app.staged);
	unsigned writes = host_write_count;
	CHECK(!lt_ota_app_switch(&app, false));
	CHECK(host_write_count == writes);
	CHECK(!lt_ota_app_is_valid(&on_a, 1));
	CHECK(boot(&trial) == 0 && !trial);
}

static void test_bad_download_after_switch(void) {
	// a second update staged after a switch, before rebooting, arrives corrupt
	lt_ota_app_t app;
	bool trial;
	fresh_device_with_staged_b(&app);
	CHECK(lt_ota_app_switch(&app, false));
	stage_image(1, 0x6000, 3, 1);
	flip_bit(1, 0x3000, 1);
	app = after_update(0);
	CHECK(!app.staged);
	CHECK(!lt_ota_app_switch(&app, false)); // reported, though boot_bank already says B
	// the bootloader refuses the corrupt trial and stays on A
	CHECK(boot(&trial) == 0 && !trial);
	CHECK(load().boot_bank == 0 && load().state[1] == LT_OTA_BANK_EMPTY);
}

static void test_restage_after_switch(void) {
	lt_ota_app_t app;
	bool trial;
	fresh_device_with_staged_b(&app);
	CHECK(lt_ota_app_switch(&app, false));

	// a second update staged before rebooting replaces the first
	stage_image(1, 0x6000, 3, 1);
	app = after_update(0);
	CHECK(lt_ota_app_switch(&app, false));
	lt_ota_meta_t m = load();
	CHECK(m.boot_bank == 1 && m.state[1] == LT_OTA_BANK_TRIAL && m.attempts == LT_OTA_TRIAL_ATTEMPTS);
	CHECK(boot(&trial) == 1 && trial);
}

static void test_switch_write_failure(void) {
	lt_ota_app_t app;
	bool trial;
	fresh_device_with_staged_b(&app);
	host_fail_write_at = 1;
	CHECK(!lt_ota_app_switch(&app, false));
	CHECK(!have_meta());
	CHECK(boot(&trial) == 0 && !trial);
	CHECK(lt_ota_app_switch(&app, false)); // retry succeeds
	CHECK(boot(&trial) == 1 && trial);
}

static uint32_t desc_crc(uint8_t bank) {
	const lt_ota_image_desc_t *d =
		(const lt_ota_image_desc_t *)(host_flash_mem() + lt_ota_bank_offset(bank) + LT_OTA_IMAGE_DESC_OFFSET);
	return d->crc_hi << 16 | d->crc_lo;
}

static void test_reflashed_running_bank(void) {
	// bank A re-flashed over SWD without erasing the metadata
	bool trial;
	host_flash_reset();
	flash_image(0, IMG_LEN, 1);
	lt_ota_meta_t m = {0};
	m.boot_bank		= 0;
	m.state[0]		= LT_OTA_BANK_CONFIRMED;
	m.image_crc[0]	= desc_crc(0);
	CHECK(lt_ota_meta_store(ops, &m));
	uint32_t new_a = flash_image(0, IMG_LEN + 0x100, 4);
	CHECK(boot(&trial) == 0 && !trial); // unknown to the metadata: last resort

	stage_image(1, 0x5000, 2, 1);
	lt_ota_app_t app = after_update(0);
	CHECK(lt_ota_app_switch(&app, false));
	// the running image is recorded as itself, VALID, not as the CONFIRMED old one
	m = load();
	CHECK(m.state[0] == LT_OTA_BANK_VALID && m.image_crc[0] == new_a);
	CHECK(m.state[1] == LT_OTA_BANK_TRIAL && m.image_crc[1] == desc_crc(1));
	for (int i = 0; i < LT_OTA_TRIAL_ATTEMPTS; i++)
		CHECK(boot(&trial) == 1 && trial);
	CHECK(boot(&trial) == 0 && !trial); // reverts to the re-flashed image
	CHECK(load().boot_bank == 0);
}

static void test_interrupted_update_into_valid_bank(void) {
	// Running A CONFIRMED, B VALID (v1, rolled back from). An update writes v3
	// completely into B; the switch's metadata write never happens (power
	// loss, failed write, or lt_ota_end() never reached).
	lt_ota_app_t app;
	bool trial;
	fresh_device_with_staged_b(&app);
	CHECK(lt_ota_app_switch(&app, false));
	CHECK(boot(&trial) == 1 && trial);
	CHECK(lt_ota_app_confirm(&(lt_ota_app_t){.ops = ops, .running = 1}));
	CHECK(lt_ota_app_switch(&(lt_ota_app_t){.ops = ops, .running = 1}, false)); // rollBack() to A
	CHECK(boot(&trial) == 0 && trial);
	lt_ota_app_t on_a = app_on(0, false);
	CHECK(lt_ota_app_confirm(&on_a));
	CHECK(load().state[1] == LT_OTA_BANK_VALID || load().state[1] == LT_OTA_BANK_CONFIRMED);
	CHECK(lt_ota_app_is_valid(&on_a, 1));

	stage_image(1, 0x6000, 3, 1);
	CHECK(check_bank(1) == LT_OTA_IMAGE_OK);
	// after the reboot: v3 is not offered for rollback, and rollBack() refuses it
	CHECK(boot(&trial) == 0 && !trial);
	unsigned writes = host_write_count;
	CHECK(!lt_ota_app_is_valid(&on_a, 1));
	CHECK(!lt_ota_app_switch(&on_a, false));
	CHECK(host_write_count == writes);
	// if A ever fails verification, v3 is not booted as known-good
	corrupt_bank(0, 0x2000);
	CHECK(boot(&trial) == 0 && !trial); // last resort, boot_bank first
	memset(host_flash_mem() + lt_ota_bank_offset(0), 0x00, 8);
	CHECK(boot(&trial) == 1 && !trial); // last resort; nothing stored
	CHECK(host_write_count == writes);
	// the same update completed properly is a normal trial
	flash_image(0, IMG_LEN, 1);
	CHECK(lt_ota_app_switch(&(lt_ota_app_t){.ops = ops, .running = 0, .staged = true}, false));
	CHECK(boot(&trial) == 1 && trial);
}

static void test_interrupted_update_while_on_trial(void) {
	// Running B as an unconfirmed trial, A VALID (v1). An update writes v3
	// completely into A and the switch never happens: B's trial does not end
	// in v3 being booted as known-good.
	lt_ota_app_t app;
	bool trial;
	fresh_device_with_staged_b(&app);
	CHECK(lt_ota_app_switch(&app, false));
	CHECK(boot(&trial) == 1 && trial);
	stage_image(0, 0x5000, 3, 0);
	lt_ota_app_t on_b = app_on(1, false);
	CHECK(!lt_ota_app_is_valid(&on_b, 0));
	CHECK(!lt_ota_app_switch(&on_b, false));
	for (int i = 0; i < LT_OTA_TRIAL_ATTEMPTS + 2; i++)
		CHECK(boot(&trial) == 1 && trial);
	lt_ota_meta_t m = load();
	CHECK(m.boot_bank == 1 && m.state[1] == LT_OTA_BANK_TRIAL && m.state[0] == LT_OTA_BANK_VALID);
	// B can still confirm itself
	CHECK(lt_ota_app_confirm(&on_b));
	CHECK(boot(&trial) == 1 && !trial);
}

static void test_unverified_running_image(void) {
	// The running image fails its own check (started by the bootloader's last
	// resort, e.g. after a flash bit error). An update is still activated, but
	// the running bank is recorded EMPTY: there is nothing to revert to.
	bool trial;
	host_flash_reset();
	flash_image(0, IMG_LEN, 1);
	corrupt_bank(0, 0x3000);
	CHECK(boot(&trial) == 0 && !trial);
	stage_image(1, 0x4000, 2, 1);
	lt_ota_app_t app = after_update(0);
	CHECK(lt_ota_app_switch(&app, false));
	lt_ota_meta_t m = load();
	CHECK(m.boot_bank == 1 && m.state[1] == LT_OTA_BANK_TRIAL && m.state[0] == LT_OTA_BANK_EMPTY);
	// a trial that never confirms keeps booting with the watchdog armed
	for (int i = 0; i < LT_OTA_TRIAL_ATTEMPTS + 3; i++)
		CHECK(boot(&trial) == 1 && trial);
	// ...and one that confirms is done
	CHECK(lt_ota_app_confirm(&(lt_ota_app_t){.ops = ops, .running = 1}));
	CHECK(boot(&trial) == 1 && !trial);

	// a revert from such an image also retires the cancelled update, which
	// the bootloader would otherwise boot in place of the unusable running bank
	host_flash_reset();
	flash_image(0, IMG_LEN, 1);
	corrupt_bank(0, 0x3000);
	stage_image(1, 0x4000, 2, 1);
	app = after_update(0);
	CHECK(lt_ota_app_switch(&app, false));
	CHECK(lt_ota_app_switch(&app, true));
	m = load();
	CHECK(m.boot_bank == 0 && m.state[0] == LT_OTA_BANK_EMPTY && m.state[1] == LT_OTA_BANK_EMPTY);
	CHECK(boot(&trial) == 0 && !trial);	   // last resort
	CHECK(lt_ota_app_switch(&app, false)); // still staged this boot: can be activated again
	CHECK(boot(&trial) == 1 && trial);
}

static void test_confirm_does_not_undo_rollback(void) {
	// a rollback requested while running an unconfirmed trial
	lt_ota_app_t app;
	bool trial;
	fresh_device_with_staged_b(&app);
	CHECK(lt_ota_app_switch(&app, false));
	CHECK(boot(&trial) == 1 && trial);
	lt_ota_app_t on_b = app_on(1, false);
	CHECK(lt_ota_app_switch(&on_b, false)); // back to A, as a trial
	unsigned writes = host_write_count;
	CHECK(lt_ota_app_confirm(&on_b));
	CHECK(host_write_count == writes);
	CHECK(load().boot_bank == 0);
	CHECK(boot(&trial) == 0 && trial);
}

static void test_revert_onto_running_trial(void) {
	// running B as an unconfirmed trial, an update staged into A, then cancelled
	lt_ota_app_t app;
	bool trial;
	fresh_device_with_staged_b(&app);
	CHECK(lt_ota_app_switch(&app, false));
	CHECK(boot(&trial) == 1 && trial);
	stage_image(0, 0x4800, 3, 0);
	lt_ota_app_t on_b = after_update(1);
	CHECK(on_b.staged);
	CHECK(lt_ota_app_switch(&on_b, false));
	CHECK(lt_ota_app_switch(&on_b, true));
	CHECK(lt_ota_app_stored(&on_b) == 1);
	lt_ota_meta_t m = load();
	CHECK(m.boot_bank == 1 && m.state[1] == LT_OTA_BANK_TRIAL && m.attempts == LT_OTA_TRIAL_ATTEMPTS);
	// B keeps booting as a trial and can still be confirmed
	CHECK(boot(&trial) == 1 && trial);
	CHECK(load().state[1] == LT_OTA_BANK_TRIAL);
	CHECK(lt_ota_app_confirm(&on_b));
	CHECK(boot(&trial) == 1 && !trial);

	// the same after Update.rollBack() from an unconfirmed trial
	fresh_device_with_staged_b(&app);
	CHECK(lt_ota_app_switch(&app, false));
	CHECK(boot(&trial) == 1 && trial);
	on_b = app_on(1, false);
	CHECK(lt_ota_app_switch(&on_b, false)); // back to A
	CHECK(lt_ota_app_switch(&on_b, true));	// cancelled
	CHECK(boot(&trial) == 1 && trial);
	CHECK(load().state[1] == LT_OTA_BANK_TRIAL);
}

static void test_aborted_update(void) {
	// lt_fal_ota_written() reports nothing unless lt_ota_end() saw the whole update
	bool trial;
	host_flash_reset();
	flash_image(0, IMG_LEN, 1);
	stage_partial(1, 0x6000, 0x800, 2, 1); // the first 2 KiB of an update, descriptor included
	lt_ota_app_t app = app_on(0, false);
	CHECK(!lt_ota_app_switch(&app, false));
	CHECK(!have_meta());
	CHECK(boot(&trial) == 0 && !trial);
}

static void test_blank_meta_after_ota(void) {
	// meta erased (pio run -t upload, but bank A not rewritten) after B was
	// confirmed: both verify, so bank A boots; running A, B is not offered
	lt_ota_app_t app;
	bool trial;
	fresh_device_with_staged_b(&app);
	CHECK(lt_ota_app_switch(&app, false));
	CHECK(boot(&trial) == 1 && trial);
	CHECK(lt_ota_app_confirm(&(lt_ota_app_t){.ops = ops, .running = 1}));
	memset(host_flash_mem() + FLASH_META_OFFSET, 0xFF, FLASH_META_LENGTH);
	CHECK(boot(&trial) == 0 && !trial);
	lt_ota_app_t on_a = app_on(0, false);
	CHECK(lt_ota_app_stored(&on_a) == 0);
	CHECK(!lt_ota_app_is_valid(&on_a, 1));
	// a new update still works from there
	stage_image(1, 0x5800, 6, 1);
	app = after_update(0);
	CHECK(lt_ota_app_switch(&app, false));
	CHECK(boot(&trial) == 1 && trial);
}

int main(void) {
	test_first_ota_crash_loop();
	test_confirm();
	test_revert_before_reboot();
	test_rollback_after_reboot();
	test_bad_staged_images();
	test_bad_download_over_known_bank();
	test_bad_download_after_switch();
	test_restage_after_switch();
	test_switch_write_failure();
	test_reflashed_running_bank();
	test_interrupted_update_into_valid_bank();
	test_interrupted_update_while_on_trial();
	test_unverified_running_image();
	test_confirm_does_not_undo_rollback();
	test_revert_onto_running_trial();
	test_aborted_update();
	test_blank_meta_after_ota();
	return 0;
}
