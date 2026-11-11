#pragma once

#error "Don't include this file directly"

// The builder (LoadDefines) turns each "#define KEY VALUE" line into a global
// -D flag. Only single-token values, and no comments on a #define line.

// lwIP 2.1.2 from the Gecko SDK. mbedTLS is compiled only for the WF200 Secure
// Link and MD5, so there is no TLS client.
#define LT_HAS_FLASH	 1
#define LT_HAS_FREERTOS	 1
#define LT_HAS_LWIP		 1
#define LT_HAS_LWIP2	 1
#define LT_HAS_MBEDTLS	 0
#define LT_HAS_OTA		 1
#define LT_HAS_PRINTF	 1
#define LT_HW_DEEP_SLEEP 0
#define LT_HW_WATCHDOG	 1
#define LT_HW_WIFI		 1

// FreeRTOS heap_4 has no realloc(): use malloc + copy + free.
#define LT_HEAP_FUNC xPortGetFreeHeapSize
#define LT_REMALLOC	 1

// The chip is programmed over SWD; there is no UART download mode to reboot into.
#define LT_AUTO_DOWNLOAD_REBOOT 0
