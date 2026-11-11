/* Copyright (c) Andrew Rankin 2026-06-12. */

// USART0 is the console: printf()/the logger (putchar_p) and SerialClass both
// use it. This file is the single owner of its init state, so a printf() after
// Serial.begin() does not re-run USART_InitAsync() (which resets the baud rate,
// the frame format and IEN, killing Serial's RX interrupt).

#include <libretiny.h>
#include <sdk_private.h>

static volatile bool usart0_ready; // USART0 configured and routed
static volatile bool usart0_sent;  // a byte was written since the last init (TXC is meaningful)

// Find the USART0 location that routes 'gpio' to TX or RX; -1 if there is none.
// The AF_* macros return -1 past the last location.
static int usart0_loc(uint32_t gpio, bool tx) {
	for (int loc = 0;; loc++) {
		int port = tx ? AF_USART0_TX_PORT(loc) : AF_USART0_RX_PORT(loc);
		int pin	 = tx ? AF_USART0_TX_PIN(loc) : AF_USART0_RX_PIN(loc);
		if (port < 0 || pin < 0)
			return -1;
		if ((uint32_t)((port << 4) | pin) == gpio)
			return loc;
	}
}

bool lt_uart_init(uint32_t port, uint32_t rx, uint32_t tx, const USART_InitAsync_TypeDef *init) {
	if (port != 0)
		return false;
	int rxLoc = (rx != PIN_INVALID) ? usart0_loc(rx, false) : -1;
	int txLoc = (tx != PIN_INVALID) ? usart0_loc(tx, true) : -1;
	if ((rx != PIN_INVALID && rxLoc < 0) || (tx != PIN_INVALID && txLoc < 0) || (rxLoc < 0 && txLoc < 0))
		return false;

	USART_InitAsync_TypeDef defaults = USART_INITASYNC_DEFAULT;
	if (!init)
		init = &defaults;

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	CMU_ClockEnable(cmuClock_GPIO, true);
	CMU_ClockEnable(cmuClock_USART0, true);
	if (txLoc >= 0)
		GPIO_PinModeSet(LT_GPIO_PORT(tx), LT_GPIO_PIN(tx), gpioModePushPull, 1); // idle high
	if (rxLoc >= 0)
		GPIO_PinModeSet(LT_GPIO_PORT(rx), LT_GPIO_PIN(rx), gpioModeInput, 0);

	// USART_InitAsync() starts with USART_Reset(), which also clears IEN,
	// ROUTELOC0 and ROUTEPEN; the caller re-enables any interrupts it needs.
	USART_InitAsync(USART0, init);
	uint32_t routeLoc = 0;
	uint32_t routePen = 0;
	if (rxLoc >= 0) {
		routeLoc |= (uint32_t)rxLoc << _USART_ROUTELOC0_RXLOC_SHIFT;
		routePen |= USART_ROUTEPEN_RXPEN;
	}
	if (txLoc >= 0) {
		routeLoc |= (uint32_t)txLoc << _USART_ROUTELOC0_TXLOC_SHIFT;
		routePen |= USART_ROUTEPEN_TXPEN;
	}
	USART0->ROUTELOC0 = routeLoc;
	USART0->ROUTEPEN  = routePen;

	usart0_ready = true;
	usart0_sent	 = false;
	CORE_EXIT_ATOMIC();
	return true;
}

void lt_uart_write(uint32_t port, uint8_t c) {
	if (port != 0)
		return;
	// lt_init_family() initialises USART0, so this only triggers for output
	// printed before that (e.g. an early fault dump).
	if (!usart0_ready && !lt_uart_init(0, PIN_SERIAL0_RX, PIN_SERIAL0_TX, NULL))
		return;
	USART_Tx(USART0, c); // waits for TXBL
	usart0_sent = true;
}

void lt_uart_flush(uint32_t port) {
	// TXC reads 0 until the first frame after an init completes, so only wait
	// for it when something was actually written.
	if (port != 0 || !usart0_ready || !usart0_sent)
		return;
	while (!(USART0->STATUS & USART_STATUS_TXC)) {}
}
