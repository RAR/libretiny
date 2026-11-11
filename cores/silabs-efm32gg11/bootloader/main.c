/* Copyright (c) Andrew Rankin 2026-06-12. */

/*
 * First-stage bootloader for silabs-efm32gg11, linked at FLASH_BOOT_OFFSET.
 *
 * The vendor startup runs SystemInit and the copy/zero tables, then calls
 * main() (built with -D__START=main). The core stays on the reset HFRCO.
 * lt_ota_boot_run() picks the bank (policy in base/ota/lt_ota_boot.h); a TRIAL
 * image is started with the watchdog armed so that a hang is reset and counted
 * against its attempts. lt_ota_confirm() in the application disarms it.
 */

#include "em_cmu.h"
#include "em_device.h"
#include "em_msc.h"
#include "em_wdog.h"
#include "lt_ota_boot.h"

#include <string.h>

_Static_assert(LT_OTA_META_SLOT_LEN == FLASH_PAGE_SIZE, "metadata slot must be one flash page");
_Static_assert(LT_OTA_RAM_BASE == SRAM_BASE && LT_OTA_RAM_SIZE == SRAM_SIZE, "SRAM window mismatch");

// Flash is memory-mapped at 0; erase/write through emlib MSC (placed in RAM,
// see EM_MSC_RUN_FROM_RAM in the family builder).
static int btl_read(uint32_t off, void *buf, uint32_t len) {
	memcpy(buf, (const void *)(uintptr_t)off, len);
	return 0;
}

static int btl_erase(uint32_t off, uint32_t len) {
	if ((off | len) & (FLASH_PAGE_SIZE - 1))
		return -1;
	for (uint32_t addr = off; addr < off + len; addr += FLASH_PAGE_SIZE) {
		if (MSC_ErasePage((uint32_t *)(uintptr_t)addr) != mscReturnOk)
			return -1;
	}
	return 0;
}

static int btl_write(uint32_t off, const void *buf, uint32_t len) {
	if ((off | len) & 0x3u)
		return -1;
	return MSC_WriteWord((uint32_t *)(uintptr_t)off, buf, len) == mscReturnOk ? 0 : -1;
}

static const lt_ota_flash_ops_t btl_flash_ops = {btl_read, btl_erase, btl_write};

// Whether a watchdog armed before the reset (a trial, or lt_wdt_enable() with
// a period down to ~9 ms) survives a system reset request is not documented,
// so it is switched off before the bank selection, whose CRC passes take a
// noticeable fraction of a second.
static void disarm_watchdog(void) {
	CMU_ClockEnable(cmuClock_CORELE, true);
	WDOGn_Enable(DEFAULT_WDOG, false);
}

static void arm_trial_watchdog(void) {
	// WDOG registers are on the low-energy peripheral bus clock (HFBUSCLKEN0.LE).
	CMU_ClockEnable(cmuClock_CORELE, true);
	WDOG_Init_TypeDef init = WDOG_INIT_DEFAULT;
	init.enable			   = true;
	init.debugRun		   = false; // frozen while the core is halted by a debugger
	init.em2Run			   = true;	// a hang in a sleep mode still counts
	init.em3Run			   = true;
	init.lock			   = false; // lt_ota_confirm() and lt_wdt_enable() reconfigure it
	init.clkSel			   = wdogClkSelULFRCO;
	// Longest period: 262145 cycles of the ~1 kHz ULFRCO, about 4.4 minutes
	// (the ULFRCO is uncalibrated, so the real period varies between parts).
	init.perSel = wdogPeriod_256k;
	WDOGn_Init(DEFAULT_WDOG, &init);
	WDOGn_Feed(DEFAULT_WDOG);
}

// Load the image's initial stack pointer (r0) and branch to its reset handler
// (r1). Naked, so no compiler-generated code touches the stack after MSP changes.
__attribute__((naked, noreturn, noinline)) static void start_image(
	__attribute__((unused)) uint32_t msp,
	__attribute__((unused)) uint32_t pc
) {
	__asm volatile("msr msp, r0\n"
				   "bx r1\n");
}

static void jump_to_bank(uint8_t bank) {
	const uint32_t *vectors = (const uint32_t *)(uintptr_t)lt_ota_bank_offset(bank);
	MSC_Deinit();
	// The image's SystemInit sets VTOR itself; set it here too so a fault
	// before that point uses the image's handlers rather than ours.
	SCB->VTOR = (uint32_t)(uintptr_t)vectors;
	__DSB();
	__ISB();
	start_image(vectors[0], vectors[1]);
}

int main(void) {
	disarm_watchdog();
	MSC_Init();
	bool trial = false;
	int bank   = lt_ota_boot_run(&btl_flash_ops, &trial);
	if (bank < 0) {
		// nothing bootable: stay here, reachable over SWD
		MSC_Deinit();
		while (1) {}
	}
	if (trial)
		arm_trial_watchdog();
	jump_to_bank((uint8_t)bank);
	while (1) {}
}
