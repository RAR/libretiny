/* Copyright (c) Andrew Rankin 2026-06-12. */
#include "lt_gpio_irq_lines.h"

#define OWNER(gpio) ((uint16_t)((gpio) + 1u))

// Line owned by 'gpio' within its group, or -1.
static int find(const lt_gpio_irq_lines_t *lines, uint32_t gpio) {
	unsigned int base = (unsigned int)(gpio & 0xCu);
	for (unsigned int i = base; i < base + 4; i++) {
		if (lines->owner[i] == OWNER(gpio))
			return (int)i;
	}
	return -1;
}

int lt_gpio_irq_lines_claim(lt_gpio_irq_lines_t *lines, uint32_t gpio) {
	if (gpio > 0xFFu)
		return -1;
	unsigned int pin  = (unsigned int)(gpio & 0xFu);
	unsigned int base = pin & ~3u;
	int line		  = find(lines, gpio);
	if (line < 0 && lines->owner[pin] == 0)
		line = (int)pin;
	for (unsigned int i = base; line < 0 && i < base + 4; i++) {
		if (lines->owner[i] == 0)
			line = (int)i;
	}
	if (line >= 0)
		lines->owner[line] = OWNER(gpio);
	return line;
}

int lt_gpio_irq_lines_release(lt_gpio_irq_lines_t *lines, uint32_t gpio) {
	if (gpio > 0xFFu)
		return -1;
	int line = find(lines, gpio);
	if (line >= 0)
		lines->owner[line] = 0;
	return line;
}
