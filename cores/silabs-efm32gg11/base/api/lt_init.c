/* Copyright (c) WGM160P-LibreTiny port 2026-05-26.
 *
 * Phase 1 lt_init_family() for silabs-efm32gg11.
 *
 * NOTE: LibreTiny's common API (cores/common/base/api/lt_init.h) declares
 * lt_init_family(), lt_init_variant(), lt_init_arduino() as weak hooks;
 * the family implements lt_init_family(). The plan's working title "lt_init"
 * was a shorthand — the actual symbol is lt_init_family.
 */

#include "lt_family.h"
#include "em_chip.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_usart.h"
#include "em_timer.h"

#include <libretiny.h>

void lt_init_family(void) {
    CHIP_Init();

    // Clock tree: HFXO (50 MHz) -> DPLL -> 72 MHz HFCLK.
    // Plan called for CMU_HFXOINIT_WSTK_DEFAULT, but that macro isn't defined
    // in GSDK 4.5.0's em_cmu.h for this part. Fall back to CMU_HFXOINIT_DEFAULT
    // and let the SDK choose conservative trim values; the WSTK's 50 MHz crystal
    // works fine with the defaults.
    CMU_HFXOInit_TypeDef hfxoInit = CMU_HFXOINIT_DEFAULT;
    CMU_HFXOInit(&hfxoInit);
    CMU_OscillatorEnable(cmuOsc_HFXO, true, true);
    CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFXO);

    // DPLL configuration: 72 MHz from 50 MHz HFXO.
    // Ratio = (N+1) / (M+1).  N=71, M=49  ->  72/50 = 1.44, target 72 MHz.
    // NOTE: GG11's CMU_DPLLInit_TypeDef (em_cmu.h Series-1 variant) has no
    // .ditherEn member — spread-spectrum is controlled via ssInterval/ssAmplitude
    // (kept at zero here to disable SSC). Series-2 parts have the ditherEn field;
    // we are not on Series 2.
    CMU_DPLLInit_TypeDef dpllInit = {
        .frequency = 72000000U,
        .n = 71U,
        .m = 49U,
        .ssInterval = 0U,
        .ssAmplitude = 0U,
        .refClk = cmuDPLLClkSel_Hfxo,
        .edgeSel = cmuDPLLEdgeSel_Fall,
        .lockMode = cmuDPLLLockMode_Phase,
        .autoRecover = true,
    };
    if (!CMU_DPLLLock(&dpllInit)) {
        // DPLL failed to lock; remain on HFXO 50 MHz (degraded but functional).
        // Will be visible in lt_cpu_get_freq() return value.
    }

    // Enable peripheral clocks needed by Phase 1 surface.
    CMU_ClockEnable(cmuClock_GPIO, true);
    CMU_ClockEnable(cmuClock_USART0, true);  // WSTK virtual COM (USART0 LOC1)
    CMU_ClockEnable(cmuClock_TIMER0, true);  // micros() resolution

    // Pre-configure VCOM USART pins so printf/Serial work as soon as they're called.
    // PE7 = TX, PE6 = RX (USART0 LOC1).
    GPIO_PinModeSet(gpioPortE, 7U, gpioModePushPull, 1U);  // TX idle high
    GPIO_PinModeSet(gpioPortE, 6U, gpioModeInput,    0U);
}
