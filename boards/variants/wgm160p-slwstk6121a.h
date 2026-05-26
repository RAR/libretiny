/* This file was hand-written for the WGM160P SLWSTK6121A board.
 *
 * boardgen 0.12.0 cannot currently process this board JSON because:
 *   - the pin "role" enum (NC/IO/UART/SPI/...) does not include 'PCB' or
 *     'ARD' which we use for the WSTK silkscreen and Arduino-name fields,
 *   - the Board pydantic model requires build.family and upload.flash_size
 *     which our minimal silabs-efm32gg11 base does not yet provide,
 *   - the schema has no notion of EFM32 PA/PB/.. port-prefixed pin names.
 *
 * Follow-up: extend boardgen with an EFM32 pin role + 'ARD' key (or
 * normalise board JSONs to the existing role enum) so this file can be
 * regenerated from the JSON. Tracked under Task 35 of the EFM32GG11 port.
 *
 * EFM32 pin encoding: (port_index << 4) | pin_number
 *   port A=0, B=1, C=2, D=3, E=4, F=5
 *   PA4=0x04 PA5=0x05 PD6=0x36 PD8=0x38 PE6=0x46 PE7=0x47
 * This matches the encoding cores/silabs-efm32gg11/arduino/src/ArduinoFamily.h
 * is expected to use (Task 24).
 */

#pragma once

// clang-format off

// Pins
// ----
#define PINS_COUNT         6    // Broken-out, Arduino-named pins on the WSTK
#define NUM_DIGITAL_PINS   6    // Digital inputs/outputs
#define NUM_ANALOG_INPUTS  0    // ADC inputs (none wired on the WSTK header in Phase 1)
#define NUM_ANALOG_OUTPUTS 0    // PWM & DAC outputs (none in Phase 1)
#define PINS_GPIO_MAX      0x47 // Last usable encoded GPIO number (PE7)

// Serial ports
// ------------
// USART0 LOC1 routes to the on-board VCOM (J-Link CDC-ACM) bridge.
#define PIN_SERIAL0_RX  0x46u // PE6
#define PIN_SERIAL0_TX  0x47u // PE7
#define PINS_SERIAL0_RX {0x46u}
#define PINS_SERIAL0_TX {0x47u}

// Pin function macros
// -------------------
#define PIN_PA4 0x04u // PA4
#define PIN_PA5 0x05u // PA5
#define PIN_PD6 0x36u // PD6
#define PIN_PD8 0x38u // PD8
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
#define PIN_D0 0x04u // PA4 — WSTK LED0 (active-high)
#define PIN_D1 0x05u // PA5 — WSTK LED1 (active-high)
#define PIN_D2 0x36u // PD6 — WSTK BTN0 (active-low)
#define PIN_D3 0x38u // PD8 — WSTK BTN1 (active-low)
#define PIN_D4 0x46u // PE6 — VCOM RX (USART0 LOC1)
#define PIN_D5 0x47u // PE7 — VCOM TX (USART0 LOC1)

// Board-specific Arduino names
// ----------------------------
#define LED0        PIN_D0
#define LED1        PIN_D1
#define BTN0        PIN_D2
#define BTN1        PIN_D3
#define LED_BUILTIN PIN_D0

// Static pin names
// ----------------
static const unsigned char D0 = PIN_D0;
static const unsigned char D1 = PIN_D1;
static const unsigned char D2 = PIN_D2;
static const unsigned char D3 = PIN_D3;
static const unsigned char D4 = PIN_D4;
static const unsigned char D5 = PIN_D5;
