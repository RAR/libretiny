/* This file is hand-written for the Enel X JuiceBox 40 (WGM160P host).
 *
 * See wgm160p-slwstk6121a.h for why boardgen 0.12.0 cannot autogenerate the
 * EFM32 variant (port-collapsing pin model + ARD label assumptions).
 *
 * Pin roles come from the bench-confirmed Gecko OS GPIO usage map of a live
 * JuiceBox 40 (probed via gpio_get/gpio_set over the unauthenticated runtime
 * shell, 2026-05). The JuiceBox repurposes PB5 away from the module's nominal
 * UART_CTS to an RGB LED channel. Per-channel color and drive polarity are not
 * yet probed; for bring-up any channel toggling proves GPIO is alive.
 *
 * EFM32 pin encoding: (port_index << 4) | pin_number
 *   port A=0, B=1, C=2, D=3, E=4, F=5
 *   PB3=0x13 PB5=0x15 PB6=0x16 PC4=0x24 PE6=0x46 PE7=0x47
 * This matches cores/silabs-efm32gg11/arduino/src/ArduinoFamily.h.
 */

#pragma once

// clang-format off

// Pins
// ----
#define PINS_COUNT         6    // Arduino-named pins exposed on the JuiceBox host
#define NUM_DIGITAL_PINS   6    // Digital inputs/outputs
#define NUM_ANALOG_INPUTS  0    // ADC inputs (none mapped in Phase 1)
#define NUM_ANALOG_OUTPUTS 0    // PWM & DAC outputs (none in Phase 1)
#define PINS_GPIO_MAX      0x47 // Last usable encoded GPIO number (PE7)

// Serial ports
// ------------
// USART0 LOC1 routes to the WGM160P console UART (module pins 46/47).
#define PIN_SERIAL0_RX  0x46u // PE6
#define PIN_SERIAL0_TX  0x47u // PE7
#define PINS_SERIAL0_RX {0x46u}
#define PINS_SERIAL0_TX {0x47u}

// Pin function macros
// -------------------
#define PIN_PB3 0x13u // PB3 (RGB LED ch)
#define PIN_PB5 0x15u // PB5 (RGB LED ch)
#define PIN_PB6 0x16u // PB6 (RGB LED ch)
#define PIN_PC4 0x24u // PC4 (factory_reset line)
#define PIN_PE6 0x46u // PE6 (USART0 LOC1 RX)
#define PIN_PE7 0x47u // PE7 (USART0 LOC1 TX)
#define PIN_RX  0x46u // PE6
#define PIN_TX  0x47u // PE7

// Port availability
// -----------------
#define HAS_SERIAL0             1
#define SERIAL_INTERFACES_COUNT 1

// Arduino pin names
// -----------------
#define PIN_D0 0x13u // PB3 — RGB LED ch (Gecko GPIO 8)
#define PIN_D1 0x15u // PB5 — RGB LED ch (Gecko GPIO 10)
#define PIN_D2 0x16u // PB6 — RGB LED ch (Gecko GPIO 11)
#define PIN_D3 0x24u // PC4 — factory_reset line (Gecko GPIO 16)
#define PIN_D4 0x46u // PE6 — console UART RX (USART0 LOC1)
#define PIN_D5 0x47u // PE7 — console UART TX (USART0 LOC1)

// Board-specific Arduino names
// ----------------------------
#define LED0        PIN_D0
#define LED1        PIN_D1
#define LED2        PIN_D2
#define BTN0        PIN_D3
#define LED_BUILTIN PIN_D0

// Static pin names
// ----------------
static const unsigned char D0 = PIN_D0;
static const unsigned char D1 = PIN_D1;
static const unsigned char D2 = PIN_D2;
static const unsigned char D3 = PIN_D3;
static const unsigned char D4 = PIN_D4;
static const unsigned char D5 = PIN_D5;
