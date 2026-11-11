/* Copyright (c) Andrew Rankin 2026-06-12. */

#include <ArduinoPrivate.h>

// The EXTI line allocator and the GPIO_EVEN/GPIO_ODD dispatcher are in the
// base core (base/port/lt_gpio_irq.c), so framework = base has them too.

void attachInterruptParam(pin_size_t interruptNumber, voidFuncPtrParam callback, PinStatus mode, void *param) {
	pinCheckGetData(interruptNumber, PIN_IRQ, );

	bool rising, falling;
	switch (mode) {
		case LOW:
			LT_W("LOW interrupts not supported, setting FALLING instead");
			// fall through
		case FALLING:
			rising	= false;
			falling = true;
			break;
		case HIGH:
			LT_W("HIGH interrupts not supported, setting RISING instead");
			// fall through
		case RISING:
			rising	= true;
			falling = false;
			break;
		case CHANGE:
			rising	= true;
			falling = true;
			break;
		default:
			return;
	}

	// GPIO can't be used together with PWM
	pinRemoveMode(pin, PIN_PWM);
	// The edge detector needs the input buffer, and pins reset to Disabled.
	// A mode the sketch already chose (pull-up, output, ...) is kept.
	if (!pinEnabled(pin, PIN_GPIO))
		pinMode(interruptNumber, INPUT);

	if (lt_gpio_irq_attach(pin->gpio, callback, param, rising, falling) < 0)
		return;
	pinEnable(pin, PIN_IRQ);
	data->irqMode = mode;
}

void detachInterrupt(pin_size_t interruptNumber) {
	pinModeRemove(interruptNumber, PIN_IRQ);
}
