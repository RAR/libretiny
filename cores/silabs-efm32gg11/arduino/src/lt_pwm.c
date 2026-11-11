/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "lt_pwm.h"

lt_pwm_cfg_t lt_pwm_calc(
	uint32_t timer_clk_hz,
	uint32_t period_us,
	uint8_t res_bits,
	uint32_t value,
	uint32_t counter_max
) {
	lt_pwm_cfg_t cfg;

	// Period in timer ticks at each prescaler; it must fit counter_max.
	uint32_t div   = 1u;
	uint64_t ticks = 0;
	for (;;) {
		ticks = (uint64_t)timer_clk_hz * period_us / 1000000ULL / div;
		if (ticks <= counter_max || div >= LT_PWM_MAX_PRESCALE)
			break;
		div <<= 1;
	}
	if (ticks > counter_max)
		ticks = counter_max; // too slow even at /1024
	if (ticks < 2)
		ticks = 2; // too fast: keep a usable period
	cfg.prescale_div = div;
	cfg.top			 = (uint32_t)(ticks - 1);

	uint32_t maxval = (res_bits >= 32) ? 0xFFFFFFFFu : ((1u << res_bits) - 1u);
	if (maxval == 0)
		maxval = 1;
	if (value > maxval)
		value = maxval;
	// value == maxval gives ticks == top + 1: never matched, steady high.
	cfg.compare = (uint32_t)((uint64_t)value * ticks / maxval);
	return cfg;
}
