/* Copyright (c) Andrew Rankin 2026-06-12. */
// Test assertion that does not depend on NDEBUG.
#pragma once

#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond)                                                                  \
	do {                                                                             \
		if (!(cond)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			exit(1);                                                                 \
		}                                                                            \
	} while (0)
