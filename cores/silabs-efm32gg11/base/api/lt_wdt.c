/* Copyright (c) Andrew Rankin 2026-06-12. */

/*
 * Watchdog on WDOG0, clocked from the ULFRCO (nominally 1 kHz and always
 * running; the emlib default LFRCO is not started by this core). The hardware
 * offers 16 periods of 2^(n+3)+1 cycles, 9 to 262145, i.e. about 9 ms to
 * 4.4 minutes; the ULFRCO is not calibrated, so real periods vary between
 * parts. Timeouts are rounded up to the next period.
 *
 * The bootloader starts a TRIAL image with the watchdog already running at the
 * longest period (bootloader/main.c). lt_ota_confirm() stops it through
 * lt_wdt_release_boot() unless the application has taken the watchdog over
 * with lt_wdt_enable().
 */

#include <libretiny.h>
#include <sdk_private.h>

#include <em_wdog.h>

static bool app_enabled;

static uint32_t period_cycles(uint32_t sel) {
	return (1u << (sel + 3)) + 1;
}

bool lt_wdt_enable(uint32_t timeout_ms) {
	if (timeout_ms == 0 || timeout_ms > period_cycles(wdogPeriod_256k))
		return false;
	uint32_t sel = wdogPeriod_9;
	while (period_cycles(sel) < timeout_ms)
		sel++;

	// WDOG registers are on the low-energy peripheral bus clock (HFBUSCLKEN0.LE)
	CMU_ClockEnable(cmuClock_CORELE, true);
	if (WDOGn_IsLocked(DEFAULT_WDOG))
		return false;
	if (WDOGn_IsEnabled(DEFAULT_WDOG)) {
		// Running already (the bootloader's trial watchdog, or an earlier call).
		// Clear the counter first, so that a shorter period does not expire as
		// soon as it is written.
		WDOGn_Feed(DEFAULT_WDOG);
		WDOGn_SyncWait(DEFAULT_WDOG);
	}
	WDOG_Init_TypeDef init = WDOG_INIT_DEFAULT;
	init.enable			   = true;
	init.clkSel			   = wdogClkSelULFRCO;
	init.perSel			   = (WDOG_PeriodSel_TypeDef)sel;
	// debugRun stays false: the counter stops while a debugger halts the core
	WDOGn_Init(DEFAULT_WDOG, &init);
	WDOGn_Feed(DEFAULT_WDOG);
	app_enabled = true;
	return true;
}

void lt_wdt_disable(void) {
	CMU_ClockEnable(cmuClock_CORELE, true);
	WDOGn_Enable(DEFAULT_WDOG, false);
	app_enabled = false;
}

void lt_wdt_feed(void) {
	WDOGn_Feed(DEFAULT_WDOG);
}

// Stop the watchdog the bootloader armed for a TRIAL image (see lt_ota.c).
void lt_wdt_release_boot(void) {
	if (app_enabled)
		return;
	CMU_ClockEnable(cmuClock_CORELE, true);
	if (WDOGn_IsEnabled(DEFAULT_WDOG))
		WDOGn_Enable(DEFAULT_WDOG, false);
}
