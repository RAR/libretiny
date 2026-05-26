/* wiring_irq.c for silabs-efm32gg11.
 *
 * GPIO EXTI dispatch for attachInterrupt/detachInterrupt.
 *
 * EFM32 GPIO EXTI lines are indexed by pin number (0-15) only — pins on
 * different ports with the same pin number share an EXTI line. This is a
 * Phase 1 limitation documented in the family README: a second
 * attachInterrupt() on a same-numbered pin on a different port replaces
 * the previously-registered callback.
 *
 * The Arduino API surface implemented here is attachInterruptParam +
 * detachInterrupt. The plain attachInterrupt(pin, voidFuncPtr, mode)
 * variant is provided by cores/common/arduino/src/wiring/wiring_irq.c
 * which trampolines into attachInterruptParam with param=NULL.
 */

#include "ArduinoFamily.h"
#include "em_core.h"
#include "em_gpio.h"
#include <Arduino.h>

/* 16 EXTI lines, indexed by pin number only. */
#define EFM32_EXTI_LINES 16

static volatile voidFuncPtrParam g_callbacks[EFM32_EXTI_LINES] = {0};
static void *volatile g_params[EFM32_EXTI_LINES]			   = {0};

static void dispatch(uint32_t mask) {
	for (uint32_t i = 0; i < EFM32_EXTI_LINES; ++i) {
		if ((mask & (1U << i)) && g_callbacks[i]) {
			g_callbacks[i](g_params[i]);
		}
	}
	GPIO_IntClear(mask);
}

void attachInterruptParam(pin_size_t pin, voidFuncPtrParam cb, PinStatus mode, void *param) {
	uint32_t idx = pin_index(pin);
	if (idx >= EFM32_EXTI_LINES)
		return;

	bool rising	 = (mode == RISING) || (mode == CHANGE);
	bool falling = (mode == FALLING) || (mode == CHANGE);

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	g_callbacks[idx] = cb;
	g_params[idx]	 = param;
	/* EFM32 EXTI: intNo == pin number (0-15); port selects which port
	 * drives the line. Last-writer-wins across ports for a given idx. */
	GPIO_ExtIntConfig(pin_port(pin), idx, idx, rising, falling, true);
	CORE_EXIT_ATOMIC();

	/* EVEN pin numbers -> GPIO_EVEN_IRQn; ODD pin numbers -> GPIO_ODD_IRQn. */
	NVIC_EnableIRQ((idx % 2 == 0) ? GPIO_EVEN_IRQn : GPIO_ODD_IRQn);
}

void detachInterrupt(pin_size_t pin) {
	uint32_t idx = pin_index(pin);
	if (idx >= EFM32_EXTI_LINES)
		return;
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	GPIO_IntDisable(1U << idx);
	g_callbacks[idx] = NULL;
	g_params[idx]	 = NULL;
	CORE_EXIT_ATOMIC();
}

void GPIO_EVEN_IRQHandler(void) {
	uint32_t mask = GPIO_IntGetEnabled() & 0x5555U; /* even bits */
	dispatch(mask);
}

void GPIO_ODD_IRQHandler(void) {
	uint32_t mask = GPIO_IntGetEnabled() & 0xAAAAU; /* odd bits */
	dispatch(mask);
}
