/* Copyright (c) Andrew Rankin 2026-06-12. */

// analogWrite() on TIMER/WTIMER compare channels in PWM mode. The timer
// arithmetic is in lt_pwm.c (host-tested).
//
// There is no analogRead(): em_adc is not part of this build, so no variant
// pin may advertise PIN_ADC.

#include <ArduinoPrivate.h>

#include <em_timer.h>

#include "lt_pwm.h"

// Routes a pin can use, as (timer, CC channel, ROUTELOC location). The pin
// each route drives comes from the device's alternate-function tables, so a
// row cannot name the wrong pin. The variant marks those pins PIN_PWM. Each
// route needs a timer of its own, because TOP (the period) is per timer.
// clang-format off
#define LT_PWM_ROUTES(X) \
	X(WTIMER0, 2, 6) /* PB5 */ \
	X(TIMER2,  0, 4) /* PB6 */ \
	X(WTIMER1, 2, 2) /* PD8 */
// clang-format on

// The AF_* names are pasted from the timer name before it expands, so these
// macros must not pass 't' on to another macro.
#define PWM_ROUTE_ROW(t, c, l)	 {(AF_##t##_CC##c##_PORT(l) << 4) | AF_##t##_CC##c##_PIN(l), t, cmuClock_##t, c, l},
#define PWM_ROUTE_VALID(t, c, l) ((c) <= 3 && AF_##t##_CC##c##_PIN(l) >= 0) &&

typedef struct {
	uint8_t gpio; // (port << 4) | pin
	TIMER_TypeDef *timer;
	CMU_Clock_TypeDef clock;
	uint8_t cc;	 // compare/capture channel, 0..3
	uint8_t loc; // CCxLOC value
} pwm_route_t;

static const pwm_route_t pwm_routes[] = {LT_PWM_ROUTES(PWM_ROUTE_ROW)};

_Static_assert(LT_PWM_ROUTES(PWM_ROUTE_VALID) 1, "PWM route: CC channel must be 0..3 and the location must exist");
// The routing code below relies on this register layout.
_Static_assert(
	_TIMER_ROUTELOC0_CC1LOC_SHIFT == 8 && _TIMER_ROUTELOC0_CC3LOC_SHIFT == 24 &&
		TIMER_ROUTEPEN_CC3PEN == (TIMER_ROUTEPEN_CC0PEN << 3),
	"TIMER CCxLOC fields must be 8 bits apart and CCxPEN bits adjacent"
);

static const pwm_route_t *pwm_route(uint32_t gpio) {
	for (unsigned int i = 0; i < sizeof(pwm_routes) / sizeof(pwm_routes[0]); i++) {
		if (pwm_routes[i].gpio == gpio)
			return &pwm_routes[i];
	}
	return NULL;
}

void analogWrite(pin_size_t pinNumber, int value) {
	pinCheckGetInfo(pinNumber, PIN_PWM, );
	const pwm_route_t *route = pwm_route(pin->gpio);
	if (!route)
		return;

	// GPIO can't be used together with PWM. The route overrides the GPIO
	// output, so the pin's GPIO mode is simply dropped, not reset.
	pinRemoveMode(pin, PIN_IRQ);
	pinDisable(pin, PIN_GPIO);

	TIMER_TypeDef *timer = route->timer;
	CMU_ClockEnable(route->clock, true);
	lt_pwm_cfg_t cfg = lt_pwm_calc(
		CMU_ClockFreqGet(route->clock),
		(uint32_t)_analogWritePeriod,
		(uint8_t)_analogWriteResolution,
		value < 0 ? 0 : (uint32_t)value,
		TIMER_MaxCount(timer)
	);
	uint32_t presc = (uint32_t)__builtin_ctz(cfg.prescale_div); // TIMER_Prescale_TypeDef is log2(div)

	if (pinEnabled(pin, PIN_PWM) && ((timer->CTRL & _TIMER_CTRL_PRESC_MASK) >> _TIMER_CTRL_PRESC_SHIFT) == presc) {
		// Running at the right prescaler: buffered values load at the next
		// wrap, so the change is glitch-free.
		TIMER_TopBufSet(timer, cfg.top);
		TIMER_CompareBufSet(timer, route->cc, cfg.compare);
		return;
	}

	// First use, or the period (analogWriteFrequency()) needs another
	// prescaler: set the timer up from scratch.
	GPIO_PinModeSet(LT_GPIO_PORT(route->gpio), LT_GPIO_PIN(route->gpio), gpioModePushPull, 0);

	TIMER_InitCC_TypeDef ccInit = TIMER_INITCC_DEFAULT;
	ccInit.mode					= timerCCModePWM;
	TIMER_InitCC(timer, route->cc, &ccInit);

	// Direct and buffered values, so that no stale buffered value from the
	// previous setup is loaded at the next wrap.
	TIMER_TopSet(timer, cfg.top);
	TIMER_TopBufSet(timer, cfg.top);
	TIMER_CompareSet(timer, route->cc, cfg.compare);
	TIMER_CompareBufSet(timer, route->cc, cfg.compare);

	uint32_t shift	 = _TIMER_ROUTELOC0_CC1LOC_SHIFT * route->cc;
	timer->ROUTELOC0 = (timer->ROUTELOC0 & ~(_TIMER_ROUTELOC0_CC0LOC_MASK << shift)) | ((uint32_t)route->loc << shift);
	timer->ROUTEPEN |= TIMER_ROUTEPEN_CC0PEN << route->cc;

	TIMER_Init_TypeDef init = TIMER_INIT_DEFAULT;
	init.prescale			= (TIMER_Prescale_TypeDef)presc;
	TIMER_Init(timer, &init); // resets CNT and starts the timer

	pinEnable(pin, PIN_PWM);
}

void pwmRemove(PinInfo *pin) {
	const pwm_route_t *route = pwm_route(pin->gpio);
	if (!route)
		return;
	// Hand the pin back to GPIO control (push-pull, DOUT 0 as set above). The
	// timer keeps running unrouted until the next analogWrite() sets it up again.
	route->timer->ROUTEPEN &= ~(TIMER_ROUTEPEN_CC0PEN << route->cc);
}
