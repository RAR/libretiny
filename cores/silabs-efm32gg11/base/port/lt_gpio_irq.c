/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "lt_gpio_irq.h"

#include "lt_gpio_irq_lines.h"

#include <libretiny.h>
#include <sdk_private.h>

#include <FreeRTOS.h>

// Line ownership, and the callback of each line. Written with interrupts
// masked; the GPIO_EVEN/GPIO_ODD handlers read a line's callback and param
// together, also masked, so they never see a half-updated pair.
static lt_gpio_irq_lines_t lines;

static struct {
	lt_gpio_irq_cb_t callback;
	void *param;
} handlers[LT_GPIO_IRQ_LINES];

int lt_gpio_irq_attach(uint32_t gpio, lt_gpio_irq_cb_t callback, void *param, bool rising, bool falling) {
	// Also rejects PIN_INVALID (port 15 does not exist).
	if (gpio > 0xFFu || !GPIO_PORT_PIN_VALID(LT_GPIO_PORT(gpio), LT_GPIO_PIN(gpio))) {
		LT_E("GPIO %lu has no interrupt", (unsigned long)gpio);
		return -1;
	}
	unsigned int pin = LT_GPIO_PIN(gpio);

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	int line = lt_gpio_irq_lines_claim(&lines, gpio);
	if (line >= 0) {
		handlers[line].callback = callback;
		handlers[line].param	= param;
		// Selects the port and pin for the line, sets the edges, clears a
		// pending flag and enables the line.
		GPIO_ExtIntConfig(LT_GPIO_PORT(gpio), pin, (unsigned int)line, rising, falling, true);
	}
	CORE_EXIT_ATOMIC();

	if (line < 0) {
		unsigned int group = pin & ~3u;
		LT_E("No free EXTI line for P%c%u (lines %u-%u in use)", 'A' + (char)(gpio >> 4), pin, group, group + 3);
		return -1;
	}

	// Callbacks may use FreeRTOS ...FromISR() calls, so the priority must not
	// be above configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (vPortValidateInterruptPriority).
	IRQn_Type irqn = (line % 2 == 0) ? GPIO_EVEN_IRQn : GPIO_ODD_IRQn;
	NVIC_SetPriority(irqn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
	NVIC_EnableIRQ(irqn);
	return line;
}

void lt_gpio_irq_detach(uint32_t gpio) {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	int line = lt_gpio_irq_lines_release(&lines, gpio);
	if (line >= 0) {
		GPIO_IntDisable(1u << line);
		GPIO_ExtIntConfig(LT_GPIO_PORT(gpio), LT_GPIO_PIN(gpio), (unsigned int)line, false, false, false);
		handlers[line].callback = NULL;
		handlers[line].param	= NULL;
	}
	CORE_EXIT_ATOMIC();
	// The NVIC lines stay enabled: other EXTI lines may share them, and a
	// handler with no enabled line never runs.
}

static void dispatch(uint32_t flags) {
	// Clear before calling: an edge arriving during a callback then raises
	// the flag again instead of being lost.
	GPIO_IntClear(flags);
	for (unsigned int i = 0; i < LT_GPIO_IRQ_LINES; i++) {
		if (!(flags & (1u << i)))
			continue;
		// A callback may detach or re-attach other lines.
		CORE_DECLARE_IRQ_STATE;
		CORE_ENTER_ATOMIC();
		lt_gpio_irq_cb_t callback = handlers[i].callback;
		void *param				  = handlers[i].param;
		CORE_EXIT_ATOMIC();
		if (!callback)
			continue;
		if (param)
			callback(param);
		else
			((void (*)(void))callback)();
	}
}

void GPIO_EVEN_IRQHandler(void) {
	dispatch(GPIO_IntGetEnabled() & 0x5555u);
}

void GPIO_ODD_IRQHandler(void) {
	dispatch(GPIO_IntGetEnabled() & 0xAAAAu);
}
