/* Copyright (c) Andrew Rankin 2026-06-12. */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Reflected IEEE CRC-32 (zlib/PNG polynomial), table-driven.
 *
 * Returns the running, non-finalized accumulator so spans can be chained.
 * Seed with 0xFFFFFFFF and XOR the final result with 0xFFFFFFFF:
 * crc = lt_crc32(0xFFFFFFFF, p, n) ^ 0xFFFFFFFF.
 */
uint32_t lt_crc32(uint32_t crc, const void *data, size_t len);
