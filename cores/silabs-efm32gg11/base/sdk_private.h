/* Copyright (c) Andrew Rankin 2026-06-12. */

#pragma once

// Family-private header: Gecko SDK (emlib + CMSIS) includes and helpers shared
// by the family's own sources. It is not included by libretiny.h or Arduino.h,
// so the SDK's object-like macros (GPIO, TIMER0, RTC, USB, ETH, LCD, CAN0, ...)
// do not leak into user code. ArduinoPrivate.h (via wiring_private.h) includes
// it for the family's Arduino sources.

#include <stdbool.h>
#include <stdint.h>

#include <em_chip.h>
#include <em_cmu.h>
#include <em_core.h>
#include <em_device.h>
#include <em_emu.h>
#include <em_gpio.h>
#include <em_usart.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Pin numbers in the board variant are (port << 4) | pin, with port A = 0.
#define LT_GPIO_PORT(gpio) ((GPIO_Port_TypeDef)((gpio) >> 4))
#define LT_GPIO_PIN(gpio)  ((unsigned int)((gpio) & 0xFu))

// lt_device.c: latch and clear the reset cause. lt_init_family() calls it first.
void lt_device_latch_reset_cause(void);

// lt_uart.c: the only owner of USART init state, shared by printf()/the logger
// and SerialClass. Only port 0 (USART0) exists. rx/tx are variant pin numbers
// (PIN_INVALID for none); init == NULL means 115200 8N1.
bool lt_uart_init(uint32_t port, uint32_t rx, uint32_t tx, const USART_InitAsync_TypeDef *init);
// Blocking write of one byte; initialises the port with the variant's
// PIN_SERIAL0_* pins and 115200 8N1 if nothing has initialised it yet.
void lt_uart_write(uint32_t port, uint8_t c);
// Wait until everything written so far has left the shift register.
void lt_uart_flush(uint32_t port);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
