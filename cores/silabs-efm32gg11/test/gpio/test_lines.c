/* Copyright (c) Andrew Rankin 2026-06-12. */
// Host unit tests for the EXTI line allocator (lt_gpio_irq_lines.c).
#include "check.h"
#include "lt_gpio_irq_lines.h"

#include <stdbool.h>
#include <string.h>

#define GPIO(port, pin) ((uint32_t)(((port) << 4) | (pin)))
#define PA				0
#define PB				1
#define PC				2
#define PD				3
#define PE				4

// Lines claimed by the reference model below, for the randomised test.
static int model_line[256];

// Every owned line belongs to a pin of its group, no pin owns two lines, and
// the table agrees with the model.
static void check_invariants(const lt_gpio_irq_lines_t *t) {
	int seen[256];
	memset(seen, 0, sizeof(seen));
	for (int i = 0; i < LT_GPIO_IRQ_LINES; i++) {
		if (t->owner[i] == 0)
			continue;
		uint32_t gpio = t->owner[i] - 1u;
		CHECK(gpio <= 0xFF);
		CHECK((gpio & 0xCu) == ((unsigned)i & ~3u));
		CHECK(seen[gpio] == 0);
		seen[gpio] = 1;
		CHECK(model_line[gpio] == i);
	}
	for (int g = 0; g < 256; g++)
		CHECK(seen[g] == (model_line[g] >= 0));
}

int main(void) {
	// A zero-initialised table is all free; line == pin comes first.
	{
		lt_gpio_irq_lines_t t;
		memset(&t, 0, sizeof(t));
		for (unsigned pin = 0; pin < 16; pin++)
			CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PA, pin)) == (int)pin);
		// Every line taken: any other pin fails, re-claiming succeeds.
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PB, 0)) == -1);
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PA, 5)) == 5);
	}

	// Up to four pins of one group, on any ports and any pins of the group.
	{
		lt_gpio_irq_lines_t t = {{0}};
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PA, 8)) == 8);
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PB, 8)) == 9);	// line 8 taken: lowest free
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PC, 9)) == 10);	// line 9 taken
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PD, 11)) == 11); // its own number is free
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PE, 10)) == -1); // group 8-11 full
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PA, 10)) == -1);
		// Other groups are unaffected.
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PE, 2)) == 2);
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PE, 12)) == 12);
		// Re-claiming returns the line already owned, not line == pin.
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PB, 8)) == 9);
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PC, 9)) == 10);
	}

	// Release frees exactly the pin's own line.
	{
		lt_gpio_irq_lines_t t = {{0}};
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PA, 0)) == 0);
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PB, 0)) == 1);
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PC, 0)) == 2);
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PD, 0)) == 3);
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PA, 1)) == -1);
		CHECK(lt_gpio_irq_lines_release(&t, GPIO(PA, 1)) == -1); // owns nothing
		CHECK(lt_gpio_irq_lines_release(&t, GPIO(PE, 0)) == -1);
		CHECK(lt_gpio_irq_lines_release(&t, GPIO(PB, 0)) == 1);
		CHECK(t.owner[0] != 0 && t.owner[1] == 0 && t.owner[2] != 0 && t.owner[3] != 0);
		CHECK(lt_gpio_irq_lines_release(&t, GPIO(PB, 0)) == -1); // already released
		// The freed line goes to the next pin of the group.
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PA, 1)) == 1);
		CHECK(lt_gpio_irq_lines_release(&t, GPIO(PA, 0)) == 0);
		CHECK(lt_gpio_irq_lines_claim(&t, GPIO(PE, 3)) == 0);
	}

	// Pin numbers outside (port << 4) | pin are rejected and change nothing.
	// 0xFF (PIN_INVALID) fits the encoding; the dispatcher rejects its port.
	{
		lt_gpio_irq_lines_t t = {{0}};
		CHECK(lt_gpio_irq_lines_claim(&t, 0x100) == -1);
		CHECK(lt_gpio_irq_lines_claim(&t, 0xFFFFFFFFu) == -1);
		CHECK(lt_gpio_irq_lines_release(&t, 0x100) == -1);
		for (int i = 0; i < LT_GPIO_IRQ_LINES; i++)
			CHECK(t.owner[i] == 0);
		CHECK(lt_gpio_irq_lines_claim(&t, 0xFF) == 15);
		CHECK(lt_gpio_irq_lines_release(&t, 0xFF) == 15);
	}

	// Random claims and releases against a reference model of the rule.
	{
		lt_gpio_irq_lines_t t = {{0}};
		for (int g = 0; g < 256; g++)
			model_line[g] = -1;
		uint32_t rng = 12345;
		for (int step = 0; step < 200000; step++) {
			rng			  = rng * 1103515245u + 12345u;
			uint32_t gpio = (rng >> 8) & 0xFF;
			bool release  = (rng >> 20) & 1;
			unsigned pin  = gpio & 0xF;
			unsigned base = pin & ~3u;
			int expect	  = -1;
			if (release) {
				expect = model_line[gpio];
				CHECK(lt_gpio_irq_lines_release(&t, gpio) == expect);
				model_line[gpio] = -1;
			} else {
				int owner[4] = {0};
				for (int g = 0; g < 256; g++) {
					if (model_line[g] >= 0 && (unsigned)model_line[g] >= base && (unsigned)model_line[g] < base + 4)
						owner[model_line[g] - base] = g + 1;
				}
				if (model_line[gpio] >= 0)
					expect = model_line[gpio];
				else if (owner[pin - base] == 0)
					expect = (int)pin;
				else {
					for (unsigned i = 0; i < 4 && expect < 0; i++) {
						if (owner[i] == 0)
							expect = (int)(base + i);
					}
				}
				CHECK(lt_gpio_irq_lines_claim(&t, gpio) == expect);
				if (expect >= 0)
					model_line[gpio] = expect;
			}
			check_invariants(&t);
		}
	}
	return 0;
}
