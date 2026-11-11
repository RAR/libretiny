/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "host_flash.h"

#include <string.h>

static uint8_t flash[HOST_FLASH_LEN];

unsigned host_fail_erase_at;
unsigned host_fail_write_at;
unsigned host_fake_erase_at;
unsigned host_erase_count;
unsigned host_write_count;

uint8_t *host_flash_mem(void) {
	return flash;
}

void host_flash_reset(void) {
	memset(flash, 0xFF, sizeof(flash));
	host_fail_erase_at = host_fail_write_at = host_fake_erase_at = 0;
	host_erase_count = host_write_count = 0;
}

static int h_read(uint32_t off, void *buf, uint32_t len) {
	if (off > HOST_FLASH_LEN || len > HOST_FLASH_LEN - off)
		return -1;
	memcpy(buf, flash + off, len);
	return 0;
}

static int h_erase(uint32_t off, uint32_t len) {
	if (off % HOST_FLASH_PAGE || len % HOST_FLASH_PAGE || off > HOST_FLASH_LEN || len > HOST_FLASH_LEN - off)
		return -1;
	host_erase_count++;
	if (host_fail_erase_at && --host_fail_erase_at == 0)
		return -1;
	if (host_fake_erase_at && --host_fake_erase_at == 0)
		return 0;
	memset(flash + off, 0xFF, len);
	return 0;
}

static int h_write(uint32_t off, const void *buf, uint32_t len) {
	if (off % 4 || len % 4 || off > HOST_FLASH_LEN || len > HOST_FLASH_LEN - off)
		return -1;
	host_write_count++;
	if (host_fail_write_at && --host_fail_write_at == 0)
		return -1;
	// NOR flash: programming can only clear bits
	const uint8_t *src = buf;
	for (uint32_t i = 0; i < len; i++)
		flash[off + i] &= src[i];
	return 0;
}

const lt_ota_flash_ops_t host_flash_ops = {h_read, h_erase, h_write};
