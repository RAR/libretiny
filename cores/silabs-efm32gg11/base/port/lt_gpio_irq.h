/* Copyright (c) Andrew Rankin 2026-06-12. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// GPIO edge interrupts (EXTI) for both frameworks: Arduino's
// attachInterrupt() is built on these, and family code uses them directly for
// pins outside the board's pin table (the WF200 WIRQ line). The dispatcher
// (lt_gpio_irq.c) owns GPIO_EVEN_IRQHandler and GPIO_ODD_IRQHandler. No SDK
// or Arduino types here: the WF200 host port includes this header too.
//
// A pin can use any of the four EXTI lines of its group, (pin & ~3) ..
// (pin & ~3) + 3, so up to four pins with the same group, on any ports, can
// have interrupts at once (lt_gpio_irq_lines.h).
//
// 'gpio' is a variant pin number, (port << 4) | pin. The callback runs in
// interrupt context at NVIC priority configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY,
// so it may use FreeRTOS ...FromISR() calls. With param == NULL it is called
// with no argument, so a void (*)(void) cast to lt_gpio_irq_cb_t also works
// (this is Arduino's voidFuncPtr / voidFuncPtrParam convention).
//
// Both functions mask interrupts while they change the line table, so they may
// be called from tasks and from interrupts, including from a callback.

// Same type as Arduino's voidFuncPtrParam.
typedef void (*lt_gpio_irq_cb_t)(void *param);

// Returns the EXTI line used, or -1 (logged) when 'gpio' is not a pin of this
// chip or all four lines of its group belong to other pins. Attaching a pin
// again updates its callback and edges and clears a pending edge. Configures
// only the edge detector: the caller sets an input mode for the pin.
int lt_gpio_irq_attach(uint32_t gpio, lt_gpio_irq_cb_t callback, void *param, bool rising, bool falling);

// Release the line owned by 'gpio' (no effect on other pins' lines).
void lt_gpio_irq_detach(uint32_t gpio);

#ifdef __cplusplus
} // extern "C"
#endif
