/* Copyright (c) WGM160P-LibreTiny port 2026-05-26.
 *
 * Phase 1 stand-alone fault dumper.
 * LibreTiny's cores/common/ does NOT expose a shared lt_panic/lt_fault hook
 * (verified by grep against v1.12.1) — bk72xx and rtl87xx don't ship their
 * own either, relying on SDK weak defaults. We provide ours explicitly to
 * surface stack frames during early bring-up.
 *
 * On a Cortex-M4, the exception entry pushes R0-R3, R12, LR, PC, xPSR onto
 * the active stack. We pick the right stack (MSP or PSP) from LR's bit 2,
 * then print the frame and reset.
 */

#include "lt_family.h"
#include <stdio.h>

static void dump_frame(uint32_t *sp, const char *kind) {
    printf("\r\n*** %s ***\r\n", kind);
    printf("  R0   = 0x%08lx\r\n", (unsigned long)sp[0]);
    printf("  R1   = 0x%08lx\r\n", (unsigned long)sp[1]);
    printf("  R2   = 0x%08lx\r\n", (unsigned long)sp[2]);
    printf("  R3   = 0x%08lx\r\n", (unsigned long)sp[3]);
    printf("  R12  = 0x%08lx\r\n", (unsigned long)sp[4]);
    printf("  LR   = 0x%08lx\r\n", (unsigned long)sp[5]);
    printf("  PC   = 0x%08lx\r\n", (unsigned long)sp[6]);
    printf("  xPSR = 0x%08lx\r\n", (unsigned long)sp[7]);
    printf("  CFSR = 0x%08lx\r\n", (unsigned long)SCB->CFSR);
    printf("  HFSR = 0x%08lx\r\n", (unsigned long)SCB->HFSR);
    NVIC_SystemReset();
}

static void __attribute__((naked, used)) common_fault_entry(const char *kind) {
    (void)kind;
    __asm volatile(
        "tst lr, #4           \n"
        "ite eq               \n"
        "mrseq r2, msp        \n"
        "mrsne r2, psp        \n"
        "mov r1, r0           \n"   /* kind into r1 */
        "mov r0, r2           \n"   /* sp into r0   */
        "b dump_frame         \n"
    );
}

void HardFault_Handler(void)  { common_fault_entry("HardFault");  while (1); }
void MemManage_Handler(void)  { common_fault_entry("MemManage");  while (1); }
void BusFault_Handler(void)   { common_fault_entry("BusFault");   while (1); }
void UsageFault_Handler(void) { common_fault_entry("UsageFault"); while (1); }
