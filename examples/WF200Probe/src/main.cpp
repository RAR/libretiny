/* WF200 SPI proof-of-life probe for the WGM160P (JuiceBox 40 host).
 *
 * Phase 2 feasibility recon: reset the in-package WF200, then read its
 * CONFIG register (ID 0) over SPI (USART3 LOC0: PA0=MOSI, PA1=MISO,
 * PA2=SCK; PA3=CS as GPIO; PF12=RESETn; PE4=WUP). Per the wfx-fullMAC
 * driver's sl_wfx_init_bus(), any readback other than 0x00000000 or
 * 0xFFFFFFFF means the part is powered, clocked, and talking.
 *
 * Results are written to `wf200_probe` (read it over SWD — no UART
 * needed) and signaled on the RGB LED: red solid = alive, blue solid =
 * no response.
 */

#include <Arduino.h>

#include "em_cmu.h"
#include "em_gpio.h"
#include "em_usart.h"

// [0] done-magic 0xCAFE0001, [1..3] CONFIG readbacks of 3 attempts
volatile uint32_t wf200_probe[4];

static uint32_t wf200_read_config(void) {
	uint32_t value = 0;
	GPIO_PinOutClear(gpioPortA, 3); // CS low
	// header: SET_READ(0x8000) | (CONFIG_REG_ID 0 << 12) | word_count(2), big-endian
	USART_SpiTransfer(USART3, 0x80);
	USART_SpiTransfer(USART3, 0x02);
	for (int i = 0; i < 4; i++)
		value = (value << 8) | USART_SpiTransfer(USART3, 0xFF);
	GPIO_PinOutSet(gpioPortA, 3); // CS high
	return value;
}

void setup() {
	CMU_ClockEnable(cmuClock_USART3, true);

	// WF200 control lines
	GPIO_PinModeSet(gpioPortE, 4, gpioModePushPull, 1);	 // WUP high
	GPIO_PinModeSet(gpioPortF, 12, gpioModePushPull, 0); // RESETn asserted
	// SPI lines (USART3 LOC0)
	GPIO_PinModeSet(gpioPortA, 0, gpioModePushPull, 0); // MOSI
	GPIO_PinModeSet(gpioPortA, 1, gpioModeInput, 0);	// MISO
	GPIO_PinModeSet(gpioPortA, 2, gpioModePushPull, 0); // SCK
	GPIO_PinModeSet(gpioPortA, 3, gpioModePushPull, 1); // CS idle high

	USART_InitSync_TypeDef spiInit = USART_INITSYNC_DEFAULT;
	spiInit.baudrate			   = 1000000; // 1 MHz probe speed
	spiInit.msbf				   = true;	  // SPI mode 0, MSB first
	USART_InitSync(USART3, &spiInit);
	USART3->ROUTELOC0 = USART_ROUTELOC0_TXLOC_LOC0 | USART_ROUTELOC0_RXLOC_LOC0 |
						USART_ROUTELOC0_CLKLOC_LOC0;
	USART3->ROUTEPEN = USART_ROUTEPEN_TXPEN | USART_ROUTEPEN_RXPEN | USART_ROUTEPEN_CLKPEN;

	// Reset pulse, then give the WF200 bootloader time to come up
	delay(10);
	GPIO_PinOutSet(gpioPortF, 12); // release RESETn
	delay(50);

	for (int i = 0; i < 3; i++) {
		wf200_probe[1 + i] = wf200_read_config();
		delay(10);
	}
	wf200_probe[0] = 0xCAFE0001;

	uint32_t v	 = wf200_probe[1];
	bool alive	 = (v != 0x00000000UL) && (v != 0xFFFFFFFFUL);
	uint8_t led	 = alive ? LED_R : LED_B;
	pinMode(led, OUTPUT);
	digitalWrite(led, HIGH);
}

void loop() {
	delay(1000);
}
