/* Copyright (c) Andrew Rankin 2026-06-12. */

/* Host-only RAM model of the EFM32GG11 internal flash for the OTA tests. */

#pragma once

#include "lt_ota_meta.h"

#include <stdio.h>
#include <stdlib.h>

#define HOST_FLASH_LEN	0x200000u
#define HOST_FLASH_PAGE 0x1000u

// Assertion that stays active under NDEBUG.
#define CHECK(cond)                                                                  \
	do {                                                                             \
		if (!(cond)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
			exit(1);                                                                 \
		}                                                                            \
	} while (0)

extern const lt_ota_flash_ops_t host_flash_ops;

// Raw flash contents (writable directly, like an SWD programmer).
uint8_t *host_flash_mem(void);
// Erase everything (all 0xFF) and clear fault injection and counters.
void host_flash_reset(void);

// Fault injection: the Nth following erase/write (1 = the next one) fails
// without touching flash. 0 disables.
extern unsigned host_fail_erase_at;
extern unsigned host_fail_write_at;
// The Nth following erase reports success without erasing (a dead page).
extern unsigned host_fake_erase_at;
// Operation counters since the last reset.
extern unsigned host_erase_count;
extern unsigned host_write_count;
