/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "host_flash.h"
#include "lt_crc32.h"

#include <string.h>

static uint32_t crc32_bitwise(uint32_t crc, const uint8_t *p, size_t len) {
	while (len--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
	}
	return crc;
}

int main(void) {
	// IEEE CRC-32 check value
	const char *s = "123456789";
	CHECK((lt_crc32(0xFFFFFFFFu, s, 9) ^ 0xFFFFFFFFu) == 0xCBF43926u);

	// chained spans equal one pass
	uint32_t a = lt_crc32(0xFFFFFFFFu, s, 4);
	a		   = lt_crc32(a, s + 4, 5);
	CHECK((a ^ 0xFFFFFFFFu) == 0xCBF43926u);

	// an empty span leaves the accumulator unchanged
	CHECK(lt_crc32(0x12345678u, s, 0) == 0x12345678u);

	// table matches the bitwise definition for every byte value and longer data
	static uint8_t buf[4096];
	for (unsigned i = 0; i < sizeof(buf); i++)
		buf[i] = (uint8_t)(i * 7 + (i >> 8));
	for (unsigned i = 0; i < 256; i++) {
		uint8_t b = (uint8_t)i;
		CHECK(lt_crc32(0xFFFFFFFFu, &b, 1) == crc32_bitwise(0xFFFFFFFFu, &b, 1));
	}
	CHECK(lt_crc32(0xFFFFFFFFu, buf, sizeof(buf)) == crc32_bitwise(0xFFFFFFFFu, buf, sizeof(buf)));
	return 0;
}
