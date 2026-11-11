/* Copyright (c) Andrew Rankin 2026-06-12. */
// mbedtls 3.5 configuration for silabs-efm32gg11. It enables only what WF200
// Secure Link (X25519 + CTR_DRBG key exchange, HMAC-SHA512, SHA-256 session
// key, AES-128-CCM records; base/wifi/sl_wfx_host_crypto.c) and the OTA MD5
// digest need. There is no TLS, X.509 or PSA, so the family sets
// LT_HAS_MBEDTLS 0. Software only; entropy comes from TRNG0 through
// mbedtls_hardware_poll() (base/wifi/lt_wfx_entropy.c). This file is found
// as "mbedtls/mbedtls_config.h" ahead of the Gecko SDK's default config.
#ifndef LT_WFX_MBEDTLS_CONFIG_H
#define LT_WFX_MBEDTLS_CONFIG_H

// Platform: bare metal, entropy from the hardware poll; libc calloc/free.
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

// X25519
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED

// Hashes and HMAC
#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C	 // OTA image digest (LT_ARD_MD5_MBEDTLS)
#define MBEDTLS_SHA256_C // session key
#define MBEDTLS_SHA512_C // HMAC-SHA512 and the entropy accumulator

// AES-CCM (CCM needs the cipher layer in mbedtls 3.x)
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CCM_C

// RNG
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

#define MBEDTLS_AES_FEWER_TABLES

#include "mbedtls/check_config.h"

#endif // LT_WFX_MBEDTLS_CONFIG_H
