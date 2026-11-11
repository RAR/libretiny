/* This file was auto-generated from generic-wgm160p.json using boardgen */

#pragma once

// clang-format off

// Pins
// ----
#define PINS_COUNT         31 // Total GPIO count
#define NUM_DIGITAL_PINS   31 // Digital inputs/outputs
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
#define PIN_PA00 0u  // PA_0
#define PIN_PA01 1u  // PA_1
#define PIN_PA02 2u  // PA_2
#define PIN_PA03 3u  // PA_3
#define PIN_PA04 4u  // PA_4
#define PIN_PA05 5u  // PA_5
#define PIN_PA06 6u  // PA_6
#define PIN_PA15 15u // PA_15
#define PIN_PB03 19u // PB_3
#define PIN_PB04 20u // PB_4
#define PIN_PB05 21u // PB_5
#define PIN_PB06 22u // PB_6
#define PIN_PB11 27u // PB_11
#define PIN_PB12 28u // PB_12
#define PIN_PB13 29u // PB_13
#define PIN_PB14 30u // PB_14
#define PIN_PC04 36u // PC_4
#define PIN_PC05 37u // PC_5
#define PIN_PD06 54u // PD_6
#define PIN_PD08 56u // PD_8
#define PIN_PE05 69u // PE_5
#define PIN_PE06 70u // PE_6
#define PIN_PE07 71u // PE_7
#define PIN_PE14 78u // PE_14
#define PIN_PE15 79u // PE_15
#define PIN_PF00 80u // PF_0
#define PIN_PF01 81u // PF_1
#define PIN_PF02 82u // PF_2
#define PIN_PF05 85u // PF_5
#define PIN_PF10 90u // PF_10
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
#define PIN_D0  78u // PE_14
#define PIN_D1  79u // PE_15
#define PIN_D2  0u  // PA_0
#define PIN_D3  1u  // PA_1
#define PIN_D4  2u  // PA_2
#define PIN_D5  3u  // PA_3
#define PIN_D6  4u  // PA_4
#define PIN_D7  5u  // PA_5
#define PIN_D8  19u // PB_3
#define PIN_D9  20u // PB_4
#define PIN_D10 21u // PB_5
#define PIN_D11 22u // PB_6
#define PIN_D12 29u // PB_13
#define PIN_D13 30u // PB_14
#define PIN_D14 27u // PB_11
#define PIN_D15 28u // PB_12
#define PIN_D16 54u // PD_6
#define PIN_D17 56u // PD_8
#define PIN_D18 82u // PF_2
#define PIN_D19 85u // PF_5
#define PIN_D20 37u // PC_5
#define PIN_D21 90u // PF_10
#define PIN_D22 91u // PF_11
#define PIN_D23 80u // PF_0
#define PIN_D24 81u // PF_1
#define PIN_D25 71u // PE_7
#define PIN_D26 70u // PE_6
#define PIN_D27 69u // PE_5
#define PIN_D28 36u // PC_4
#define PIN_D29 6u  // PA_6
#define PIN_D30 15u // PA_15

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
static const unsigned char D8 = PIN_D8;
static const unsigned char D9 = PIN_D9;
static const unsigned char D10 = PIN_D10;
static const unsigned char D11 = PIN_D11;
static const unsigned char D12 = PIN_D12;
static const unsigned char D13 = PIN_D13;
static const unsigned char D14 = PIN_D14;
static const unsigned char D15 = PIN_D15;
static const unsigned char D16 = PIN_D16;
static const unsigned char D17 = PIN_D17;
static const unsigned char D18 = PIN_D18;
static const unsigned char D19 = PIN_D19;
static const unsigned char D20 = PIN_D20;
static const unsigned char D21 = PIN_D21;
static const unsigned char D22 = PIN_D22;
static const unsigned char D23 = PIN_D23;
static const unsigned char D24 = PIN_D24;
static const unsigned char D25 = PIN_D25;
static const unsigned char D26 = PIN_D26;
static const unsigned char D27 = PIN_D27;
static const unsigned char D28 = PIN_D28;
static const unsigned char D29 = PIN_D29;
static const unsigned char D30 = PIN_D30;

// clang-format on

// Not boardgen output: keep these when regenerating.
//
// WGM160P module internals: the WF200 Wi-Fi radio and the pins it is wired to
// inside the module. None of these are module pads (WGM160P data sheet,
// table 7.1), so they are not in the pin table. Used by the Wi-Fi port only.
// Pin numbers are (port << 4) | pin, port A = 0.
//
// SDIO uses location 0, the only one the EFM32GG11 data sheet (table 5.22)
// gives SDIO high-speed priority on; lt_sdio.c requires these exact pins.
// RESET, WUP, WIRQ and LP_CLK are the pins of the Silicon Labs WF200 host
// driver's configuration for the EFM32GG11B820 + WF200 radio board (Gecko SDK,
// platform/radio/wifi/wfx_fmac_driver/config/brd4321a/sl_wfx_host_pinout.h);
// the module data sheet does not name them.
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
