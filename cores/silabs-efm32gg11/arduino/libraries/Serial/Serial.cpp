/* Copyright (c) Andrew Rankin 2026-06-12. */

// SerialClass on USART0, the console UART that the logger and printf() also
// use. USART init state belongs to lt_uart.c (base/port); this file adds the
// interrupt-driven RX ring buffer. TX is blocking.

#if LT_ARD_HAS_SERIAL || DOXYGEN

#include "SerialPrivate.h"

// Serial port -> SerialData, for the RX interrupt handler.
static SerialData *isr_data[SERIAL_INTERFACES_COUNT] = {nullptr};

void SerialClass::beginPrivate(unsigned long baudrate, uint16_t config) {
	(void)baudrate;
	(void)config;
	if (!this->data)
		return;
	if (this->port != 0) {
		LT_E("Serial%u not available: only USART0 is supported", (unsigned)this->port);
		return;
	}
	this->data->buf			= this->rxBuf;
	this->data->rx_overflow = false;
}

void SerialClass::configure(unsigned long baudrate, uint16_t config) {
	if (!this->data || this->port != 0 || baudrate == 0)
		return;

	USART_InitAsync_TypeDef init = USART_INITASYNC_DEFAULT;
	init.baudrate				 = baudrate;
	switch (config & SERIAL_DATA_MASK) {
		case SERIAL_DATA_5:
			init.databits = usartDatabits5;
			break;
		case SERIAL_DATA_6:
			init.databits = usartDatabits6;
			break;
		case SERIAL_DATA_7:
			init.databits = usartDatabits7;
			break;
		case SERIAL_DATA_8:
			init.databits = usartDatabits8;
			break;
		default:
			LT_E("Serial config 0x%x: unsupported data bits", (unsigned)config);
			return;
	}
	switch (config & SERIAL_PARITY_MASK) {
		case SERIAL_PARITY_NONE:
			init.parity = usartNoParity;
			break;
		case SERIAL_PARITY_EVEN:
			init.parity = usartEvenParity;
			break;
		case SERIAL_PARITY_ODD:
			init.parity = usartOddParity;
			break;
		default: // mark/space parity: not supported by the USART
			LT_E("Serial config 0x%x: unsupported parity", (unsigned)config);
			return;
	}
	switch (config & SERIAL_STOP_BIT_MASK) {
		case SERIAL_STOP_BIT_1:
			init.stopbits = usartStopbits1;
			break;
		case SERIAL_STOP_BIT_1_5:
			init.stopbits = usartStopbits1p5;
			break;
		case SERIAL_STOP_BIT_2:
			init.stopbits = usartStopbits2;
			break;
		default:
			LT_E("Serial config 0x%x: unsupported stop bits", (unsigned)config);
			return;
	}

	// The re-init resets the USART, including its interrupt enables.
	NVIC_DisableIRQ(USART0_RX_IRQn);
	isr_data[0]		 = nullptr;
	this->data->uart = nullptr;
	if (!lt_uart_init(this->port, this->rx, this->tx, &init)) {
		LT_E(
			"Serial%u: pins RX=%u TX=%u have no USART0 route",
			(unsigned)this->port,
			(unsigned)this->rx,
			(unsigned)this->tx
		);
		return;
	}
	this->data->uart = USART0;
	isr_data[0]		 = this->data;

	// The handler and SerialClass::read() both change the ring buffer's element
	// count. read() does so inside a FreeRTOS critical section, which masks
	// interrupts only up to configMAX_SYSCALL_INTERRUPT_PRIORITY, so the handler
	// must not run at the reset priority 0 (above it).
	NVIC_SetPriority(USART0_RX_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
	USART_IntClear(USART0, _USART_IF_MASK);
	USART_IntEnable(USART0, USART_IEN_RXDATAV);
	NVIC_ClearPendingIRQ(USART0_RX_IRQn);
	NVIC_EnableIRQ(USART0_RX_IRQn);

	this->baudrate = baudrate;
	this->config   = config;
}

void SerialClass::endPrivate() {
	if (!this->data || this->port != 0)
		return;
	NVIC_DisableIRQ(USART0_RX_IRQn);
	if (this->data->uart)
		USART_IntDisable(this->data->uart, USART_IEN_RXDATAV);
	isr_data[0]		 = nullptr;
	this->data->uart = nullptr;
	this->data->buf	 = nullptr;
	// USART0 itself stays configured: the logger and printf() keep using it.
}

void SerialClass::flush() {
	if (!this->data || !this->data->uart)
		return;
	lt_uart_flush(this->port);
}

size_t SerialClass::write(uint8_t c) {
	if (!this->data || !this->data->uart)
		return 0;
	lt_uart_write(this->port, c);
	return 1;
}

bool SerialClass_getOverflow() {
	SerialData *data = isr_data[0];
	if (data == nullptr)
		return false;
	bool v			  = data->rx_overflow;
	data->rx_overflow = false;
	return v;
}

// Overrides the weak alias to Default_Handler in startup_efm32gg11b.S.
extern "C" void USART0_RX_IRQHandler(void) {
	SerialData *data = isr_data[0];
	if (data == nullptr || data->uart == nullptr || data->buf == nullptr) {
		// Not started (or ended): drain and drop, so the interrupt clears.
		while (USART0->STATUS & USART_STATUS_RXDATAV)
			(void)USART_Rx(USART0);
		USART_IntClear(USART0, _USART_IF_MASK);
		return;
	}
	while (data->uart->STATUS & USART_STATUS_RXDATAV) {
		uint8_t b = (uint8_t)USART_Rx(data->uart);
		if (data->buf->isFull())
			data->rx_overflow = true; // byte dropped
		else
			data->buf->store_char(b);
	}
}

#endif // LT_ARD_HAS_SERIAL
