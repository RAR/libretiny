/* This file was auto-generated from wgm160p-juicebox-40.json using boardgen */

#include <Arduino.h>

#ifdef LT_VARIANT_INCLUDE
#include LT_VARIANT_INCLUDE
#endif

// clang-format off
PinInfo lt_arduino_pin_info_list[PINS_COUNT] = {
	// D0: PB05, PWM0
	{PB_5,  PIN_GPIO | PIN_IRQ | PIN_PWM,  PIN_NONE, 0},
	// D1: PB06, PWM1
	{PB_6,  PIN_GPIO | PIN_IRQ | PIN_PWM,  PIN_NONE, 0},
	// D2: PD08, PWM2
	{PD_8,  PIN_GPIO | PIN_IRQ | PIN_PWM,  PIN_NONE, 0},
	// D3: PC04
	{PC_4,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D4: PE06, UART0_RX
	{PE_6,  PIN_GPIO | PIN_IRQ | PIN_UART, PIN_NONE, 0},
	// D5: PE07, UART0_TX
	{PE_7,  PIN_GPIO | PIN_IRQ | PIN_UART, PIN_NONE, 0},
	// D6: PB03
	{PB_3,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D7: PF11
	{PF_11, PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
};

PinInfo *lt_arduino_pin_gpio_map[] = {
	[19] = &(lt_arduino_pin_info_list[6]), // PB_3 (D6)
	[21] = &(lt_arduino_pin_info_list[0]), // PB_5 (D0)
	[22] = &(lt_arduino_pin_info_list[1]), // PB_6 (D1)
	[36] = &(lt_arduino_pin_info_list[3]), // PC_4 (D3)
	[56] = &(lt_arduino_pin_info_list[2]), // PD_8 (D2)
	[70] = &(lt_arduino_pin_info_list[4]), // PE_6 (D4)
	[71] = &(lt_arduino_pin_info_list[5]), // PE_7 (D5)
	[91] = &(lt_arduino_pin_info_list[7]), // PF_11 (D7)
};
// clang-format on
