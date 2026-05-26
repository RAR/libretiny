#pragma once

#error "Don't include this file directly"

/* Phase 1 Arduino-side toggles for silabs-efm32gg11.
 *
 * LT_ARD_HAS_SERIAL is gated to 0 until T26 lands HardwareSerial.{h,cpp};
 * common's Arduino.h checks this macro to decide whether to pull in Serial.h.
 */
#define LT_ARD_HAS_SERIAL  0
#define LT_ARD_HAS_WIFI    0
#define LT_ARD_MD5_MBEDTLS 0
