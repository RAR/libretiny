/* Copyright (c) Andrew Rankin 2026-06-12. */

#include <ArduinoPrivate.h>

// millis() is the FreeRTOS tick count, so it wraps after 2^32 ms like on any
// Arduino. micros() adds the SysTick position within the current tick.
//
// A flash page erase (FAL port, OTA, FlashDB) stalls instruction fetch for
// the whole erase with interrupts enabled; SysTick interrupts falling due in
// that time collapse into one pending interrupt, so millis() falls behind by
// up to one erase time per erase.
#if configTICK_RATE_HZ != 1000
#error "millis() assumes a 1 kHz FreeRTOS tick"
#endif
#if !portTICK_TYPE_IS_ATOMIC
#error "tick_count() assumes an atomic tick counter"
#endif

// Usable from any context, including interrupts above
// configMAX_SYSCALL_INTERRUPT_PRIORITY (the logger timestamps with millis()):
// the ARM_CM4F port sets portTICK_TYPE_IS_ATOMIC, so xTaskGetTickCount() is a
// plain 32-bit load with no critical section. xTaskGetTickCountFromISR()
// would assert on such interrupts.
static uint32_t tick_count(void) {
	return xTaskGetTickCount();
}

unsigned long millis(void) {
	return tick_count();
}

unsigned long micros(void) {
	// Last value returned, for the monotonic clamp below.
	static uint32_t last;

	// SysTick counts down from LOAD to 0 once per tick (LOAD + 1 counts). The
	// FreeRTOS port starts it with the scheduler; before that micros() is 0.
	// With interrupts masked the tick count cannot move, but the counter can
	// wrap: then the SysTick interrupt is pending and the tick it will add is
	// not counted yet.
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	uint32_t ticks = tick_count();
	uint32_t load  = SysTick->LOAD;
	uint32_t val   = SysTick->VAL;
	if (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) {
		ticks++;
		val = SysTick->VAL; // re-read: the first read may predate the wrap
	}
	uint32_t us = ticks * 1000UL;
	if (load != 0) // else SysTick is not running yet and VAL is undefined
		us += (load - val) * 1000UL / (load + 1);

	// The raw value can still drop by up to one tick in two cases: from an
	// interrupt that preempted SysTick_Handler after the counter wrapped (its
	// pending bit is already cleared) but before xTickCount was incremented,
	// and while the scheduler is suspended (xTickCount is frozen and the
	// SysTick position wraps every millisecond). Never return less than the
	// previous result: a small backward step returns the previous value, so
	// micros() holds still until the real time passes it (for a suspended
	// scheduler: until it resumes). A forward step over 2^32 us is a normal
	// wrap; larger backward steps cannot happen and are passed through.
	if ((int32_t)(us - last) < 0 && (uint32_t)(last - us) <= 2000UL)
		us = last;
	last = us;
	__set_PRIMASK(primask);
	return us;
}

void delayMicroseconds(unsigned int us) {
	// Busy-wait on the DWT cycle counter: works in interrupts, with interrupts
	// masked and before the scheduler starts. Enable it on each call, since a
	// debugger may switch trace (TRCENA) off when it detaches.
	if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) || !(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
		CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
		DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	}
	while (us) {
		// Chunks of at most 1 s keep the cycle count well inside 32 bits.
		uint32_t chunk	= (us > 1000000U) ? 1000000U : us;
		uint32_t cycles = (uint32_t)((uint64_t)chunk * SystemCoreClock / 1000000U);
		uint32_t start	= DWT->CYCCNT;
		while ((uint32_t)(DWT->CYCCNT - start) < cycles) {}
		us -= chunk;
	}
}

void pinRemoveMode(PinInfo *pin, uint32_t mask) {
	if ((mask & PIN_PWM) && (pin->enabled & PIN_PWM)) {
		pwmRemove(pin);
		pinDisable(pin, PIN_PWM);
	}
	if ((mask & PIN_IRQ) && (pin->enabled & PIN_IRQ)) {
		lt_gpio_irq_detach(pin->gpio);
		pinDisable(pin, PIN_IRQ);
	}
	if ((mask & PIN_GPIO) && (pin->enabled & PIN_GPIO)) {
		// Back to the reset state: input and pull disabled.
		GPIO_PinModeSet(LT_GPIO_PORT(pin->gpio), LT_GPIO_PIN(pin->gpio), gpioModeDisabled, 0);
		pinDisable(pin, PIN_GPIO);
	}
}
