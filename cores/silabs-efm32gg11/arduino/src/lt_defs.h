#pragma once

#error "Don't include this file directly"

// MD5 comes from the mbedTLS build that the WF200 Secure Link needs (md5.c is
// in the builder's mbedtls source list); there is no TLS (LT_HAS_MBEDTLS 0).
#define LT_ARD_HAS_SERIAL  1
#define LT_ARD_HAS_WIFI	   1
#define LT_ARD_MD5_MBEDTLS 1
