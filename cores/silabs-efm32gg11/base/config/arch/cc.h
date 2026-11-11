/* Copyright (c) Andrew Rankin 2026-06-12. */
// lwIP arch/cc.h for silabs-efm32gg11 (GCC, newlib). It shadows the GSDK
// lwip-contrib FreeRTOS port's include/arch/cc.h: the family base/config
// directory comes first on the include path, the contrib include directory
// is appended by the lwIP library. (arch/sys_arch.h is not shadowed.)
//
// The contrib cc.h defines LWIP_PROVIDE_ERRNO unconditionally, which makes
// lwIP declare its own Linux-numbered errno values and `extern int errno`.
// Both clash with newlib, whose errno is a macro and whose E* values differ
// (EINPROGRESS 119 vs 115, ETIMEDOUT 116 vs 110). Common LibreTiny code
// compares errno with newlib's values, so lwIP uses newlib's errno
// (LWIP_ERRNO_STDINCLUDE in lwipopts.h).
#ifndef LT_SILABS_LWIP_ARCH_CC_H
#define LT_SILABS_LWIP_ARCH_CC_H

#include <stdio.h> // LWIP_PLATFORM_ASSERT / LWIP_PLATFORM_DIAG
#include <stdlib.h>

/* Cortex-M4 is little-endian; LITTLE_ENDIAN is provided by lwip/arch.h. */
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

/* GCC packing for protocol structs. */
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((__packed__))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

// Report the message, then go through the family's assert handler, as FreeRTOS
// configASSERT does (base/api/lt_fault.c: prints file/line, flushes the
// console, resets).
#ifdef __cplusplus
extern "C" {
#endif
void vAssertCalled(const char *file, int line);
#ifdef __cplusplus
}
#endif
#define LWIP_PLATFORM_ASSERT(x)            \
	do {                                   \
		printf("lwIP assert \"%s\"\n", x); \
		vAssertCalled(__FILE__, __LINE__); \
	} while (0)

#endif /* LT_SILABS_LWIP_ARCH_CC_H */
