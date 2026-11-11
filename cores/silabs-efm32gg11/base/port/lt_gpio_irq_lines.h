/* Copyright (c) Andrew Rankin 2026-06-12. */
// EXTI line allocation for the GPIO interrupt dispatcher (lt_gpio_irq.c).
// Pure C with no SDK dependency, so the host test in test/gpio covers it.
//
// The EFM32GG11 has 16 external interrupt lines. Line n can take any pin of
// group n & ~3, on any port (EXTIPINSEL selects one of the group's four pins,
// EXTIPSEL the port; emlib's GPIO_ExtIntConfig() asserts
// GPIO_INTNO_PIN_VALID). So up to four pins with the same pin & ~3 can have
// interrupts at once, whatever their ports.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LT_GPIO_IRQ_LINES 16

// Owner of each line: the variant pin number, (port << 4) | pin, plus one, so
// a zero-initialised table has every line free. The caller serialises access.
typedef struct {
	uint16_t owner[LT_GPIO_IRQ_LINES];
} lt_gpio_irq_lines_t;

// Takes a line for 'gpio' (at most 0xFF): the one it already owns, else the
// line numbered like the pin, else the lowest free line of the pin's group.
// Returns the line, or -1 when all four lines of the group belong to other
// pins or 'gpio' is out of range.
int lt_gpio_irq_lines_claim(lt_gpio_irq_lines_t *lines, uint32_t gpio);

// Frees the line 'gpio' owns. Returns that line, or -1 if it owns none.
int lt_gpio_irq_lines_release(lt_gpio_irq_lines_t *lines, uint32_t gpio);

#ifdef __cplusplus
} // extern "C"
#endif
