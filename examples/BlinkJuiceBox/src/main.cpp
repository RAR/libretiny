/* Phase 1 RGB-channel scan for the Enel X JuiceBox 40 (WGM160P host).
 *
 * Walks the three candidate RGB channels (PB3/PB5/PB6 = LED0/1/2) one at a
 * time, ~700 ms each, and prints which channel is active on the console UART
 * (PE6/PE7, USART0 LOC1, 115200 8N1). Whichever channel is wired to a visible
 * LED will light; an RGB LED will visibly cycle colors. This identifies the
 * channel and proves clock + GPIO + the Arduino core boot path in one flash.
 *
 * Polarity-agnostic for detection: with one channel HIGH and two LOW, an
 * active-high LED lights the selected channel and an active-low (common-anode)
 * LED lights the other two — either way the display changes every 700 ms.
 */

#include <Arduino.h>

static const uint8_t ch[3] = {LED0, LED1, LED2}; // PB3, PB5, PB6

void setup() {
	Serial.begin(115200);
	Serial.println("JuiceBox 40 WGM160P — RGB channel scan (PB3/PB5/PB6)");
	for (int i = 0; i < 3; i++)
		pinMode(ch[i], OUTPUT);
}

void loop() {
	const char *name[3] = {"PB3 (LED0)", "PB5 (LED1)", "PB6 (LED2)"};
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++)
			digitalWrite(ch[j], j == i ? HIGH : LOW);
		Serial.print("active: ");
		Serial.println(name[i]);
		delay(700);
	}
}
