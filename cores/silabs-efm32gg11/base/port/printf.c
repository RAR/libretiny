/* Copyright (c) Andrew Rankin 2026-06-12. */

#include <libretiny.h>
#include <sdk_private.h>

#include <printf/printf.h>

void putchar_(char c) {
	putchar_p(c, LT_UART_DEFAULT_LOGGER);
}

void putchar_p(char c, unsigned long port) {
	// Ports without a UART (anything but 0) are dropped by lt_uart_write().
	lt_uart_write(port, (uint8_t)c);
}
