/* Copyright (c) Andrew Rankin 2026-06-12. */

#pragma once

#include <ArduinoPrivate.h>
#include <Serial.h>

struct SerialData {
	// USART0 once configure() has set it up, else nullptr.
	USART_TypeDef *uart{nullptr};
	// SerialClass::rxBuf, for the RX interrupt handler.
	SerialRingBuffer *buf{nullptr};
	// Set by the RX interrupt when a byte is dropped because buf is full.
	volatile bool rx_overflow{false};
};

// Family extension: read and clear the RX overflow flag of Serial0.
bool SerialClass_getOverflow();
