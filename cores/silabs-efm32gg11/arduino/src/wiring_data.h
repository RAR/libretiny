/* Copyright (c) Andrew Rankin 2026-06-12. */

#pragma once

#include <ArduinoPrivate.h>

#include <lt_gpio_irq.h> // base/port: the EXTI dispatcher

#ifdef __cplusplus
extern "C" {
#endif

// Per-pin state. Nothing in it is heap-allocated: interrupt callbacks live in
// the EXTI dispatcher (base/port/lt_gpio_irq.c), PWM state in the timer registers.
struct PinData_s {
	PinMode gpioMode;
	PinStatus irqMode;
};

// wiring_analog.c: release the pin's PWM route, if it has one.
void pwmRemove(PinInfo *pin);

#ifdef __cplusplus
} // extern "C"
#endif
