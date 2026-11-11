/* Copyright (c) Andrew Rankin 2026-06-12. */

// Fatal-error handlers: Cortex-M fault dump, FreeRTOS configASSERT(), and the
// FreeRTOS stack-overflow and malloc-failed hooks. They live in base/ so that
// framework=base links too.
//
// Nothing in the program references the fault handlers: the vendor startup
// object (which owns __Vectors) provides weak `b .` defaults, so the linker
// would never pull this object out of the core archive on its own. The family
// builder links the core archive whole for that reason.

#include <libretiny.h>
#include <sdk_private.h>

#include <FreeRTOS.h>
#include <task.h>

static const char *const fault_names[] = {
	"HardFault",
	"MemManage",
	"BusFault",
	"UsageFault",
};

// Output for the fatal paths. They run on whatever stack faulted or on the
// interrupt stack (MSP, sized by _stack_size in the linker script), so they
// bypass printf() and write straight to the console UART: a few words of
// stack, no formatting engine.
static void fault_puts(const char *s) {
	while (*s)
		lt_uart_write(LT_UART_DEFAULT_LOGGER, (uint8_t)*s++);
}

static void fault_hex(uint32_t v) {
	fault_puts("0x");
	for (int shift = 28; shift >= 0; shift -= 4)
		lt_uart_write(LT_UART_DEFAULT_LOGGER, (uint8_t)"0123456789abcdef"[(v >> shift) & 0xF]);
}

static void fault_dec(uint32_t v) {
	char buf[11];
	char *p = buf + sizeof(buf);
	*--p	= '\0';
	do {
		*--p = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	fault_puts(p);
}

static void fault_reg(const char *name, uint32_t v) {
	fault_puts(name);
	fault_hex(v);
	fault_puts("\r\n");
}

// Make sure the last message is on the wire, then reset.
static void __attribute__((noreturn)) fault_reset(void) {
	lt_uart_flush(LT_UART_DEFAULT_LOGGER);
	NVIC_SystemReset();
}

void __attribute__((used)) lt_fault_dump(uint32_t *sp, uint32_t kind) {
	fault_puts("\r\n*** ");
	fault_puts(fault_names[kind & 3]);
	fault_puts(" ***\r\n");
	// The frame pointer comes from MSP/PSP as the fault left it; a corrupt one
	// must not be dereferenced (a fault in here would lock the core up). The
	// basic frame is 8 words, word-aligned, and must lie inside SRAM.
	uint32_t frame = (uint32_t)sp;
	if ((frame & 3) == 0 && frame >= SRAM_BASE && frame <= SRAM_BASE + SRAM_SIZE - 8 * sizeof(uint32_t)) {
		fault_reg("  R0    = ", sp[0]);
		fault_reg("  R1    = ", sp[1]);
		fault_reg("  R2    = ", sp[2]);
		fault_reg("  R3    = ", sp[3]);
		fault_reg("  R12   = ", sp[4]);
		fault_reg("  LR    = ", sp[5]);
		fault_reg("  PC    = ", sp[6]);
		fault_reg("  xPSR  = ", sp[7]);
	} else {
		fault_reg("  stacked frame not readable, SP = ", frame);
	}
	fault_reg("  CFSR  = ", SCB->CFSR);
	fault_reg("  HFSR  = ", SCB->HFSR);
	fault_reg("  BFAR  = ", SCB->BFAR);
	fault_reg("  MMFAR = ", SCB->MMFAR);
	fault_reset();
}

// The handlers must be naked: at exception entry LR holds EXC_RETURN (bit 2
// selects MSP or PSP) and the active SP points at the hardware-stacked frame
// {R0-R3, R12, LR, PC, xPSR}. A normal C prologue would push onto that stack
// and clobber LR before they could be read. Pick the faulting stack, pass the
// frame pointer and a fault_names index, and tail-branch into the dumper.
#define LT_FAULT_HANDLER(name, idx)                 \
	void __attribute__((naked)) name(void) {        \
		__asm volatile("tst lr, #4            \n"   \
					   "ite eq                \n"   \
					   "mrseq r0, msp         \n"   \
					   "mrsne r0, psp         \n"   \
					   "movs r1, #" #idx "    \n"   \
					   "b lt_fault_dump       \n"); \
	}

LT_FAULT_HANDLER(HardFault_Handler, 0)
LT_FAULT_HANDLER(MemManage_Handler, 1)
LT_FAULT_HANDLER(BusFault_Handler, 2)
LT_FAULT_HANDLER(UsageFault_Handler, 3)

// configASSERT() target (FreeRTOSConfig.h). A failed kernel assertion means
// corrupted state, so print where it fired and reset. With a debugger
// attached, stop on a breakpoint first so the state can be inspected;
// resuming from it resets. (Without a debugger, BKPT would escalate to a
// HardFault, hence the check.)
void vAssertCalled(const char *file, int line) {
	taskDISABLE_INTERRUPTS();
	fault_puts("\r\n*** FreeRTOS assert failed: ");
	fault_puts(file);
	fault_puts(":");
	fault_dec((uint32_t)line);
	fault_puts(" ***\r\n");
	lt_uart_flush(LT_UART_DEFAULT_LOGGER);
	if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
		__BKPT(0);
	NVIC_SystemReset();
}

// The overflowing task has already written past its stack, so memory is not
// trustworthy any more: report and reset.
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
	(void)task;
	fault_puts("\r\n*** stack overflow in task '");
	fault_puts(name);
	fault_puts("' ***\r\n");
	fault_reset();
}

// Every malloc()/new goes to the FreeRTOS heap (cores/common fixups/malloc.c),
// and pvPortMalloc() calls this on any NULL return. Only log: callers that
// handle NULL must get the chance to do so.
void vApplicationMallocFailedHook(void) {
	const char *task = "-";
	if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
		task = pcTaskGetName(NULL);
	LT_E("Heap allocation failed in task '%s', %u bytes free", task, (unsigned)xPortGetFreeHeapSize());
}
