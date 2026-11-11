# Copyright (c) Andrew Rankin 2026-06-12.
# Host unit tests for the GPIO interrupt (EXTI) line allocator. Pure C, no
# hardware.
# Run: make -C cores/silabs-efm32gg11/test/gpio
CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -g
INCLUDES = -I../../base/port
TESTS    = test_lines

.PHONY: all clean
all: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t && echo "PASS" || exit 1; done

test_lines: test_lines.c ../../base/port/lt_gpio_irq_lines.c check.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_lines.c ../../base/port/lt_gpio_irq_lines.c

clean:
	rm -f $(TESTS)
