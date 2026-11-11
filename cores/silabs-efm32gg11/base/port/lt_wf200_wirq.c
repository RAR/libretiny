/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "lt_wf200_wirq.h"

#include "lt_gpio_irq.h"

#include <libretiny.h>

bool lt_wf200_wirq_attach(void (*isr)(void)) {
	// param == NULL: the dispatcher calls isr with no argument.
	if (lt_gpio_irq_attach(PIN_WF200_WIRQ, (lt_gpio_irq_cb_t)isr, NULL, true, false) >= 0)
		return true;
	LT_WM(WIFI, "WF200 WIRQ interrupt unavailable; RX relies on polling");
	return false;
}

void lt_wf200_wirq_detach(void) {
	lt_gpio_irq_detach(PIN_WF200_WIRQ);
}
