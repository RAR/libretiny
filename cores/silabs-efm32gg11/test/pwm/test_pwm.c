/* Copyright (c) Andrew Rankin 2026-06-12. */

// Host unit tests for the PWM timer arithmetic (arduino/src/lt_pwm.c).
// Run: make -C cores/silabs-efm32gg11/test/pwm

#include "lt_pwm.h"

#include <stdio.h>
#include <stdlib.h>

// Not assert(): these checks must also run when built with -DNDEBUG.
static int failures;
#define CHECK(cond)                                                            \
	do {                                                                       \
		if (!(cond)) {                                                         \
			fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                        \
		}                                                                      \
	} while (0)

#define MAX16 0xFFFFu
#define MAX32 0xFFFFFFFFu

int main(void) {
	lt_pwm_cfg_t c;

	// 72 MHz, 50 Hz (20000 us) on a 16-bit TIMER: 1.44e6 ticks at /1 do not
	// fit, /32 gives 45000 ticks -> TOP 44999.
	c = lt_pwm_calc(72000000u, 20000u, 8, 0, MAX16);
	CHECK(c.prescale_div == 32u);
	CHECK(c.top == 44999u);

	// Same on a 32-bit WTIMER: no prescaling.
	c = lt_pwm_calc(72000000u, 20000u, 8, 0, MAX32);
	CHECK(c.prescale_div == 1u);
	CHECK(c.top == 1439999u);

	// 0 % and 100 %: compare 0, and compare top + 1 (never matched).
	c = lt_pwm_calc(72000000u, 20000u, 8, 0, MAX32);
	CHECK(c.compare == 0u);
	c = lt_pwm_calc(72000000u, 20000u, 8, 255, MAX32);
	CHECK(c.compare == c.top + 1u);
	c = lt_pwm_calc(72000000u, 20000u, 8, 255, MAX16);
	CHECK(c.compare == c.top + 1u);
	CHECK(c.compare <= MAX16); // representable in the 16-bit CC register

	// 50 %: the high time is compare ticks of top + 1.
	c = lt_pwm_calc(72000000u, 20000u, 8, 128, MAX32);
	CHECK(c.compare > 1440000u / 2u - 10000u && c.compare < 1440000u / 2u + 10000u);
	c = lt_pwm_calc(72000000u, 20000u, 10, 512, MAX32);
	CHECK(c.compare > 1440000u / 2u - 5000u && c.compare < 1440000u / 2u + 5000u);
	CHECK(lt_pwm_calc(72000000u, 20000u, 10, 1023, MAX32).compare == 1440000u);

	// 16-bit resolution, full scale, 32-bit timer: no 32-bit overflow.
	c = lt_pwm_calc(72000000u, 20000u, 16, 65535, MAX32);
	CHECK(c.top == 1439999u);
	CHECK(c.compare == 1440000u);

	// Frequency scaling: 100 Hz halves the period.
	CHECK(lt_pwm_calc(72000000u, 10000u, 8, 0, MAX32).top == 719999u);

	// Prescaler recomputation after a frequency change (analogWriteFrequency()):
	// on the 16-bit timer 50 Hz needs /32, 300 Hz /4, 1 kHz /2, 20 kHz /1.
	CHECK(lt_pwm_calc(72000000u, 20000u, 8, 0, MAX16).prescale_div == 32u);
	c = lt_pwm_calc(72000000u, 1000000u / 300u, 8, 0, MAX16);
	CHECK(c.prescale_div == 4u);
	CHECK(c.top + 1u == (uint32_t)(72000000ull * 3333u / 1000000u / 4u));
	CHECK(lt_pwm_calc(72000000u, 1000u, 8, 0, MAX16).prescale_div == 2u);
	CHECK(lt_pwm_calc(72000000u, 50u, 8, 0, MAX16).prescale_div == 1u);
	// The boundary: exactly 65535 ticks still fits at /1.
	CHECK(lt_pwm_calc(65535u, 1000000u, 8, 0, MAX16).prescale_div == 1u);

	// Does not fit even at /1024 (0.5 Hz on 16 bits): clamped, 100 % still
	// representable.
	c = lt_pwm_calc(72000000u, 2000000u, 8, 255, MAX16);
	CHECK(c.prescale_div == 1024u);
	CHECK(c.top == MAX16 - 1u);
	CHECK(c.compare == MAX16);

	// Very high frequency: at least two ticks per period.
	c = lt_pwm_calc(72000000u, 0u, 8, 255, MAX32);
	CHECK(c.top == 1u);
	CHECK(c.compare == 2u);

	// Values above full scale clamp to 100 %.
	CHECK(lt_pwm_calc(72000000u, 20000u, 8, 999, MAX32).compare == 1440000u);

	// Resolution 0 does not divide by zero.
	CHECK(lt_pwm_calc(72000000u, 20000u, 0, 1, MAX32).compare == 1440000u);

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
