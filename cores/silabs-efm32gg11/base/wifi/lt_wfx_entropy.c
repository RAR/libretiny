/* Copyright (c) Andrew Rankin 2026-06-12. */
// Entropy from the EFM32GG11 TRNG0 (NIST SP800-90B / AIS31 noise source with
// an on-chip conditioner) for two consumers:
//   - mbedtls (MBEDTLS_ENTROPY_HARDWARE_ALT): mbedtls_hardware_poll() seeds the
//     CTR_DRBG behind the Secure Link X25519 key exchange.
//   - lwIP: lt_trng_rand32() backs LWIP_RAND() (ephemeral ports, DHCP xid,
//     DNS ids).
// TRNG0 is driven directly; the Silicon Labs PSA entropy driver expects an
// SLC-generated project.

#include "em_cmu.h"
#include "em_core.h"
#include "em_device.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "mbedtls/entropy.h"

// Health-test alarms. Any of them makes the output since the last check
// suspect; a soft reset clears the alarm and the FIFO and reruns the startup
// tests.
#define LT_TRNG_ALARMS \
	(TRNG_STATUS_PREIF | TRNG_STATUS_REPCOUNTIF | TRNG_STATUS_APT64IF | TRNG_STATUS_APT4096IF | TRNG_STATUS_ALMIF)
// Soft resets allowed per word before the TRNG is declared failed.
#define LT_TRNG_MAX_RESETS 4

static bool s_started;
static bool s_lwip_failed;

static void lt_trng_soft_reset(void) {
	uint32_t ctrl  = TRNG0->CONTROL;
	TRNG0->CONTROL = ctrl | TRNG_CONTROL_SOFTRESET;
	TRNG0->CONTROL = ctrl & ~TRNG_CONTROL_SOFTRESET;
}

static void lt_trng_start(void) {
	CMU_ClockEnable(cmuClock_TRNG0, true);
	// Conditioned noise source (TESTEN/CONDBYPASS at reset values). The alarm
	// enables make the health tests latch their flags in STATUS; the TRNG0
	// NVIC line stays disabled, so no interrupt is taken.
	TRNG0->CONTROL = TRNG_CONTROL_ENABLE | TRNG_CONTROL_REPCOUNTIEN | TRNG_CONTROL_APT64IEN | TRNG_CONTROL_APT4096IEN |
					 TRNG_CONTROL_PREIEN | TRNG_CONTROL_ALMIEN;
	lt_trng_soft_reset();
	s_started = true;
}

// Fetches one conditioned 32-bit word. *alarmed (optional) is set when a
// health-test alarm fired while waiting. Returns false when no healthy word
// arrives within LT_TRNG_MAX_RESETS soft resets, each allowed about
// CORE_CLOCK / 1024 polls (the startup-test wait the vendor driver uses).
// Safe to call from several tasks: each register sequence runs in a critical
// section.
static bool lt_trng_word(uint32_t *out, bool *alarmed) {
	uint32_t budget = CMU_ClockFreqGet(cmuClock_CORE) >> 10;
	uint32_t resets = 0;
	uint32_t polls	= 0;
	for (;;) {
		bool got   = false;
		bool alarm = false;
		CORE_DECLARE_IRQ_STATE;
		CORE_ENTER_CRITICAL();
		if (!s_started)
			lt_trng_start();
		if (TRNG0->STATUS & LT_TRNG_ALARMS) {
			lt_trng_soft_reset();
			alarm = true;
		} else if (TRNG0->FIFOLEVEL != 0U) {
			*out = TRNG0->FIFO;
			got	 = true;
		}
		CORE_EXIT_CRITICAL();
		if (got)
			return true;
		if (alarm && alarmed)
			*alarmed = true;
		if (alarm || ++polls >= budget) {
			if (++resets > LT_TRNG_MAX_RESETS)
				return false;
			if (!alarm) {
				CORE_ENTER_CRITICAL();
				lt_trng_soft_reset();
				CORE_EXIT_CRITICAL();
			}
			polls = 0;
		}
	}
}

// mbedtls entropy source. Output gathered before an alarm in the same call is
// discarded; a TRNG that stops delivering fails the entropy collection (and
// with it the key exchange) instead of returning short reads forever.
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
	(void)data;
	size_t produced = 0;
	*olen			= 0;
	while (produced < len) {
		uint32_t word;
		bool alarmed = false;
		if (!lt_trng_word(&word, &alarmed))
			return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
		if (alarmed)
			produced = 0;
		for (size_t i = 0; i < sizeof(word) && produced < len; i++)
			output[produced++] = (unsigned char)(word >> (8U * i));
	}
	*olen = produced;
	return 0;
}

// LWIP_RAND(). lwIP has no way to report a failure, so if the TRNG stops
// delivering this says so once on the console and continues with an xorshift
// sequence seeded from the last TRNG word.
uint32_t lt_trng_rand32(void) {
	static uint32_t s_state = 0x6D2B79F5U;
	uint32_t word;
	if (!s_lwip_failed && lt_trng_word(&word, NULL)) {
		s_state ^= word;
		return word;
	}
	if (!s_lwip_failed) {
		s_lwip_failed = true;
		printf("TRNG0 delivers no output; LWIP_RAND() is now pseudo-random only\n");
	}
	s_state ^= s_state << 13;
	s_state ^= s_state >> 17;
	s_state ^= s_state << 5;
	return s_state;
}
