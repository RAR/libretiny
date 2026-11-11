/* Copyright (c) Andrew Rankin 2026-06-12. */

#include <libretiny.h>
#include <sdk_private.h>

lt_cpu_model_t lt_cpu_get_model(void) {
	// DEVINFO->PART DEVICE_FAMILY: 0x64 (_DEVINFO_PART_DEVICE_FAMILY_EFM32GG11B)
	// on this family, which is the chip ID of EFM32GG11 in lt_types.h.
	uint8_t family = (DEVINFO->PART & _DEVINFO_PART_DEVICE_FAMILY_MASK) >> _DEVINFO_PART_DEVICE_FAMILY_SHIFT;
	return CPU_MODEL_ENUM(FAMILY, family);
}

const char *lt_cpu_get_core_type(void) {
	return "ARM Cortex-M4F (ARMv7E-M)";
}

uint32_t lt_cpu_get_freq(void) {
	// The real core clock: 72 MHz when the HFXO or LFXO lock works, otherwise
	// whatever lt_init_family() fell back to (see there).
	return SystemCoreClockGet();
}

uint32_t lt_cpu_get_unique_id(void) {
	// Low 24 bits of the 64-bit DEVINFO unique number (the API asks for 24 bits).
	return DEVINFO->UNIQUEL & 0x00FFFFFFu;
}
