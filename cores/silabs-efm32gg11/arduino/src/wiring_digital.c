/* Copyright (c) Andrew Rankin 2026-06-12. */

#include <ArduinoPrivate.h>

void pinMode(pin_size_t pinNumber, PinMode pinMode) {
	pinCheckGetData(pinNumber, PIN_GPIO, );

	if (pinEnabled(pin, PIN_GPIO) && data->gpioMode == pinMode)
		return;

	// GPIO can't be used together with PWM
	pinRemoveMode(pin, PIN_PWM);

	GPIO_Port_TypeDef port = LT_GPIO_PORT(pin->gpio);
	unsigned int bit	   = LT_GPIO_PIN(pin->gpio);
	GPIO_Mode_TypeDef mode;
	unsigned int out; // DOUT: pull direction for inputs, level for outputs
	switch (pinMode) {
		case INPUT:
			mode = gpioModeInput;
			out	 = 0; // DOUT = 1 would enable the input glitch filter
			break;
		case INPUT_PULLUP:
			mode = gpioModeInputPull;
			out	 = 1;
			break;
		case INPUT_PULLDOWN:
			mode = gpioModeInputPull;
			out	 = 0;
			break;
		case OUTPUT:
			// Keep DOUT, so a level set before pinMode() (digitalWrite() on an
			// input sets the pull, and with it DOUT) is driven without a glitch.
			mode = gpioModePushPull;
			out	 = GPIO_PinOutGet(port, bit);
			break;
		case OUTPUT_OPENDRAIN:
			mode = gpioModeWiredAnd;
			out	 = GPIO_PinOutGet(port, bit);
			break;
		default:
			return;
	}
	GPIO_PinModeSet(port, bit, mode, out);
	pinEnable(pin, PIN_GPIO);
	data->gpioMode = pinMode;
}

void digitalWrite(pin_size_t pinNumber, PinStatus status) {
	pinCheckGetData(pinNumber, PIN_GPIO, );
	pinSetOutputPull(pin, data, pinNumber, status);
	if (status)
		GPIO_PinOutSet(LT_GPIO_PORT(pin->gpio), LT_GPIO_PIN(pin->gpio));
	else
		GPIO_PinOutClear(LT_GPIO_PORT(pin->gpio), LT_GPIO_PIN(pin->gpio));
}

PinStatus digitalRead(pin_size_t pinNumber) {
	pinCheckGetData(pinNumber, PIN_GPIO, LOW);
	pinSetInputMode(pin, data, pinNumber);
	return GPIO_PinInGet(LT_GPIO_PORT(pin->gpio), LT_GPIO_PIN(pin->gpio)) ? HIGH : LOW;
}
