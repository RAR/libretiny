/* This file was auto-generated from wgm160p-juicebox-40.json using boardgen */

#pragma once

// clang-format off

// Pins
// ----
#define PINS_COUNT         8  // Total GPIO count
#define NUM_DIGITAL_PINS   8  // Digital inputs/outputs
#define NUM_ANALOG_INPUTS  0  // ADC inputs
#define NUM_ANALOG_OUTPUTS 3  // PWM & DAC outputs
#define PINS_GPIO_MAX      91 // Last usable GPIO number

// Serial ports
// ------------
#define PIN_SERIAL0_RX  70u // PE_6
#define PIN_SERIAL0_TX  71u // PE_7
#define PINS_SERIAL0_RX {70u}
#define PINS_SERIAL0_TX {71u}

// Pin function macros
// -------------------
#define PIN_PB03 19u // PB_3
#define PIN_PB05 21u // PB_5
#define PIN_PB06 22u // PB_6
#define PIN_PC04 36u // PC_4
#define PIN_PD08 56u // PD_8
#define PIN_PE06 70u // PE_6
#define PIN_PE07 71u // PE_7
#define PIN_PF11 91u // PF_11
#define PIN_PWM0 21u // PB_5
#define PIN_PWM1 22u // PB_6
#define PIN_PWM2 56u // PD_8
#define PIN_RX0  70u // PE_6
#define PIN_TX0  71u // PE_7

// Port availability
// -----------------
#define HAS_SERIAL0             1
#define SERIAL_INTERFACES_COUNT 1

// Arduino pin names
// -----------------
#define PIN_D0 21u // PB_5
#define PIN_D1 22u // PB_6
#define PIN_D2 56u // PD_8
#define PIN_D3 36u // PC_4
#define PIN_D4 70u // PE_6
#define PIN_D5 71u // PE_7
#define PIN_D6 19u // PB_3
#define PIN_D7 91u // PF_11

// Static pin names
// ----------------
static const unsigned char D0 = PIN_D0;
static const unsigned char D1 = PIN_D1;
static const unsigned char D2 = PIN_D2;
static const unsigned char D3 = PIN_D3;
static const unsigned char D4 = PIN_D4;
static const unsigned char D5 = PIN_D5;
static const unsigned char D6 = PIN_D6;
static const unsigned char D7 = PIN_D7;

// clang-format on

// Not boardgen output: keep these when regenerating.
//
// Pin numbers are (port << 4) | pin, port A = 0 (PB5 = 0x15 = 21).
//
// Board-specific names. The RGB LED channels are active-high; PC4 is the
// factory-reset line; PF11 drives the EVSE controller's active-low RESET.
#define LED_R		PIN_D0 // PB5
#define LED_G		PIN_D1 // PB6
#define LED_B		PIN_D2 // PD8
#define LED0		LED_R
#define LED1		LED_G
#define LED2		LED_B
#define LED_BUILTIN LED_R
#define BTN0		PIN_D3 // PC4

// WGM160P module internals: the WF200 Wi-Fi radio. Used by the Wi-Fi port only;
// these pins are deliberately not in the pin table.
#define PIN_WF200_WIRQ		8u	// PA8, WF200 interrupt output
#define PIN_WF200_LP_CLK	12u // PA12, WF200 sleep clock input
#define PIN_WF200_WUP		68u // PE4, WF200 wake-up
#define PIN_WF200_RESET		92u // PF12, WF200 RESETn (active low)
#define PIN_WF200_SDIO_DAT3 72u // PE8, SDIO LOC0
#define PIN_WF200_SDIO_DAT2 73u // PE9, SDIO LOC0; also HIF_SEL (high at reset release selects SDIO)
#define PIN_WF200_SDIO_DAT1 74u // PE10, SDIO LOC0
#define PIN_WF200_SDIO_DAT0 75u // PE11, SDIO LOC0
#define PIN_WF200_SDIO_CMD	76u // PE12, SDIO LOC0
#define PIN_WF200_SDIO_CLK	77u // PE13, SDIO LOC0
