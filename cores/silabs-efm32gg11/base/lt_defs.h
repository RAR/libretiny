#pragma once

// Silicon Labs EFM32GG11B820F2048GM64 (Cortex-M4F, 72 MHz, 2 MB flash, 512 KB SRAM)
#define LT_HAS_FREERTOS    1
#define LT_HAS_LWIP        0    // Phase 2
#define LT_HAS_MBEDTLS     0    // Phase 2

// Flash + RAM
#define LT_DEV_FLASH_SIZE  (2048u * 1024u)
#define LT_DEV_RAM_SIZE    (512u * 1024u)
