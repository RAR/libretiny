/* Copyright (c) Andrew Rankin 2026-06-12. */

#pragma once

#include <PinNames.h>

// No SDK headers here: this file reaches user code through Arduino.h. Family
// sources get the Gecko SDK through ArduinoPrivate.h (sdk_private.h).

// Mask/unmask all interrupts (PRIMASK), like CMSIS __disable_irq()/__enable_irq().
#define interrupts()   __asm__ volatile("cpsie i" ::: "memory")
#define noInterrupts() __asm__ volatile("cpsid i" ::: "memory")
