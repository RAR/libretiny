/* Copyright (c) Andrew Rankin 2026-06-12. */

#pragma once

#include <lt_pins.h>

// Choose the main UART output port. USART0 is the only UART this family drives.
#ifndef LT_UART_DEFAULT_PORT
#if LT_HW_UART0
#define LT_UART_DEFAULT_PORT 0
#else
#error "No serial port is available"
#endif
#endif

// Confirm a freshly installed OTA image (stop the bootloader's rollback) once
// the Arduino setup() has returned. Define to 0 to call lt_ota_confirm() from
// the application instead, e.g. after its own health checks.
#ifndef LT_OTA_AUTO_CONFIRM
#define LT_OTA_AUTO_CONFIRM 1
#endif
