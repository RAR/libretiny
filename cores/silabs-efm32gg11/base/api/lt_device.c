/* Copyright (c) Andrew Rankin 2026-06-12. */

#include <libretiny.h>
#include <sdk_private.h>

#include <em_rmu.h>

// Reset cause of this boot, filtered by RMU_ResetCauseGet().
static uint32_t reset_cause;

void lt_device_latch_reset_cause(void) {
	// RMU->RSTCAUSE accumulates until software clears it, so read it once per
	// boot and clear it; otherwise every reset after the first power-on would
	// still report PORST.
	reset_cause = RMU_ResetCauseGet();
	RMU_ResetCauseClear();
}

void lt_get_device_mac(uint8_t *mac) {
	// This is NOT the Wi-Fi MAC. The WF200 keeps its own MAC addresses, which
	// can only be read after its firmware has booted (WiFi.macAddress()). This
	// returns a stable per-chip address from DEVINFO so lt_get_device_name()
	// and lt_cpu_get_mac_id() work before Wi-Fi starts: the factory EUI-48 when
	// it is programmed (the check and byte order the Gecko SDK network examples
	// use), else a locally administered address made from the 64-bit unique ID.
	if (DEVINFO->EUI48L != 0xFFFFFFFFUL) {
		mac[0] = (uint8_t)(DEVINFO->EUI48H >> 8);
		mac[1] = (uint8_t)(DEVINFO->EUI48H >> 0);
		mac[2] = (uint8_t)(DEVINFO->EUI48L >> 24);
		mac[3] = (uint8_t)(DEVINFO->EUI48L >> 16);
		mac[4] = (uint8_t)(DEVINFO->EUI48L >> 8);
		mac[5] = (uint8_t)(DEVINFO->EUI48L >> 0);
	} else {
		uint32_t low  = DEVINFO->UNIQUEL;
		uint32_t high = DEVINFO->UNIQUEH;
		mac[0]		  = 0x02; // locally administered, unicast
		mac[1]		  = (uint8_t)(high >> 0);
		mac[2]		  = (uint8_t)(low >> 24);
		mac[3]		  = (uint8_t)(low >> 16);
		mac[4]		  = (uint8_t)(low >> 8);
		mac[5]		  = (uint8_t)(low >> 0);
	}
}

void lt_reboot(void) {
	NVIC_SystemReset();
	while (1) {}
}

lt_reboot_reason_t lt_get_reboot_reason(void) {
	uint32_t cause = reset_cause;
	if (cause & RMU_RSTCAUSE_PORST)
		return REBOOT_REASON_POWER;
	if (cause & (RMU_RSTCAUSE_AVDDBOD | RMU_RSTCAUSE_DVDDBOD | RMU_RSTCAUSE_DECBOD))
		return REBOOT_REASON_BROWNOUT;
	if (cause & RMU_RSTCAUSE_WDOGRST)
		return REBOOT_REASON_WATCHDOG;
	if (cause & RMU_RSTCAUSE_LOCKUPRST)
		return REBOOT_REASON_CRASH;
	// NVIC_SystemReset(): lt_reboot(), the fault handlers and a debugger reset.
	if (cause & RMU_RSTCAUSE_SYSREQRST)
		return REBOOT_REASON_SOFTWARE;
	if (cause & RMU_RSTCAUSE_EXTRST)
		return REBOOT_REASON_HARDWARE;
	return REBOOT_REASON_UNKNOWN;
}
