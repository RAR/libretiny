/* This file was auto-generated from generic-wgm160p.json using boardgen */

#include <Arduino.h>

#ifdef LT_VARIANT_INCLUDE
#include LT_VARIANT_INCLUDE
#endif

// clang-format off
PinInfo lt_arduino_pin_info_list[PINS_COUNT] = {
	// D0: PE14
	{PE_14, PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D1: PE15
	{PE_15, PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D2: PA00
	{PA_0,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D3: PA01
	{PA_1,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D4: PA02
	{PA_2,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D5: PA03
	{PA_3,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D6: PA04
	{PA_4,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D7: PA05
	{PA_5,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D8: PB03
	{PB_3,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D9: PB04
	{PB_4,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D10: PB05, PWM0
	{PB_5,  PIN_GPIO | PIN_IRQ | PIN_PWM,  PIN_NONE, 0},
	// D11: PB06, PWM1
	{PB_6,  PIN_GPIO | PIN_IRQ | PIN_PWM,  PIN_NONE, 0},
	// D12: PB13
	{PB_13, PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D13: PB14
	{PB_14, PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D14: PB11
	{PB_11, PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D15: PB12
	{PB_12, PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D16: PD06
	{PD_6,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D17: PD08, PWM2
	{PD_8,  PIN_GPIO | PIN_IRQ | PIN_PWM,  PIN_NONE, 0},
	// D18: PF02
	{PF_2,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D19: PF05
	{PF_5,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D20: PC05
	{PC_5,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D21: PF10
	{PF_10, PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D22: PF11
	{PF_11, PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D23: PF00
	{PF_0,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D24: PF01
	{PF_1,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D25: PE07, UART0_TX
	{PE_7,  PIN_GPIO | PIN_IRQ | PIN_UART, PIN_NONE, 0},
	// D26: PE06, UART0_RX
	{PE_6,  PIN_GPIO | PIN_IRQ | PIN_UART, PIN_NONE, 0},
	// D27: PE05
	{PE_5,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D28: PC04
	{PC_4,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D29: PA06
	{PA_6,  PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
	// D30: PA15
	{PA_15, PIN_GPIO | PIN_IRQ,            PIN_NONE, 0},
};

PinInfo *lt_arduino_pin_gpio_map[] = {
	[0]  = &(lt_arduino_pin_info_list[2]),  // PA_0 (D2)
	[1]  = &(lt_arduino_pin_info_list[3]),  // PA_1 (D3)
	[2]  = &(lt_arduino_pin_info_list[4]),  // PA_2 (D4)
	[3]  = &(lt_arduino_pin_info_list[5]),  // PA_3 (D5)
	[4]  = &(lt_arduino_pin_info_list[6]),  // PA_4 (D6)
	[5]  = &(lt_arduino_pin_info_list[7]),  // PA_5 (D7)
	[6]  = &(lt_arduino_pin_info_list[29]), // PA_6 (D29)
	[15] = &(lt_arduino_pin_info_list[30]), // PA_15 (D30)
	[19] = &(lt_arduino_pin_info_list[8]),  // PB_3 (D8)
	[20] = &(lt_arduino_pin_info_list[9]),  // PB_4 (D9)
	[21] = &(lt_arduino_pin_info_list[10]), // PB_5 (D10)
	[22] = &(lt_arduino_pin_info_list[11]), // PB_6 (D11)
	[27] = &(lt_arduino_pin_info_list[14]), // PB_11 (D14)
	[28] = &(lt_arduino_pin_info_list[15]), // PB_12 (D15)
	[29] = &(lt_arduino_pin_info_list[12]), // PB_13 (D12)
	[30] = &(lt_arduino_pin_info_list[13]), // PB_14 (D13)
	[36] = &(lt_arduino_pin_info_list[28]), // PC_4 (D28)
	[37] = &(lt_arduino_pin_info_list[20]), // PC_5 (D20)
	[54] = &(lt_arduino_pin_info_list[16]), // PD_6 (D16)
	[56] = &(lt_arduino_pin_info_list[17]), // PD_8 (D17)
	[69] = &(lt_arduino_pin_info_list[27]), // PE_5 (D27)
	[70] = &(lt_arduino_pin_info_list[26]), // PE_6 (D26)
	[71] = &(lt_arduino_pin_info_list[25]), // PE_7 (D25)
	[78] = &(lt_arduino_pin_info_list[0]),  // PE_14 (D0)
	[79] = &(lt_arduino_pin_info_list[1]),  // PE_15 (D1)
	[80] = &(lt_arduino_pin_info_list[23]), // PF_0 (D23)
	[81] = &(lt_arduino_pin_info_list[24]), // PF_1 (D24)
	[82] = &(lt_arduino_pin_info_list[18]), // PF_2 (D18)
	[85] = &(lt_arduino_pin_info_list[19]), // PF_5 (D19)
	[90] = &(lt_arduino_pin_info_list[21]), // PF_10 (D21)
	[91] = &(lt_arduino_pin_info_list[22]), // PF_11 (D22)
};
// clang-format on
