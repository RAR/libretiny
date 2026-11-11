/* Copyright (c) Andrew Rankin 2026-06-12. */

#pragma once

// PWM timer arithmetic for analogWrite(). No SDK or Arduino dependencies, so it
// is unit-tested on the host (cores/silabs-efm32gg11/test/pwm).

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LT_PWM_MAX_PRESCALE 1024u // largest TIMER prescaler (powers of two from 1)

typedef struct {
	uint32_t prescale_div; // 1, 2, 4, ..., 1024
	uint32_t top;		   // TIMER TOP: the counter runs 0..top, so the period is top + 1 ticks
	uint32_t compare;	   // CC value: the output is high for 'compare' ticks per period
} lt_pwm_cfg_t;

// Timer setup for one PWM channel, in TIMER PWM mode (output set when the
// counter wraps to 0, cleared on compare match). Picks the smallest prescaler
// whose period fits counter_max (TIMER_MaxCount(): 0xFFFF for TIMERn,
// 0xFFFFFFFF for WTIMERn), so that compare = top + 1 is representable: that
// value never matches, which gives a steady 100 % (compare == top would
// leave one low tick per period). If the period does not fit even at /1024
// it is clamped to counter_max ticks. value 0 gives compare 0 (0 %); values
// above 2^res_bits - 1 are clamped. Intermediates are 64-bit.
lt_pwm_cfg_t lt_pwm_calc(
	uint32_t timer_clk_hz,
	uint32_t period_us,
	uint8_t res_bits,
	uint32_t value,
	uint32_t counter_max
);

#ifdef __cplusplus
}
#endif
