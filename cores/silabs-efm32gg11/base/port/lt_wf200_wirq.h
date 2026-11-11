/* Copyright (c) Andrew Rankin 2026-06-12. */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Rising-edge interrupt on the WF200 WIRQ line (the variant's PIN_WF200_WIRQ),
// through the GPIO interrupt dispatcher. For the WF200 host port
// (base/wifi/sl_wfx_host.c), which is built without the variant header, so it
// cannot name the pin. 'isr' runs in interrupt context. Attaching again
// replaces it. Only the edge detector is configured; lt_wf200_pin_wirq_input()
// (lt_sdio.h) sets the pin mode.
//
// Returns false (logged) when every EXTI line of the pin's group belongs to
// another pin; WIRQ edges then go unnoticed.
bool lt_wf200_wirq_attach(void (*isr)(void));
void lt_wf200_wirq_detach(void);

#ifdef __cplusplus
} // extern "C"
#endif
