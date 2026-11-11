/* Copyright (c) Andrew Rankin 2026-06-12. */

#include <libretiny.h>
#include <sdk_private.h>

#include <em_rmu.h>

// The GSDK startup code calls lt_main() (builder: -D__START=lt_main), which
// calls this before anything else.
void lt_init_family(void) {
	CHIP_Init();
	lt_device_latch_reset_cause();
	// A Cortex-M lockup (e.g. a fault inside the HardFault handler) does not
	// reset the chip by default (RMU_CTRL.LOCKUPRMODE resets to DISABLED), it
	// just stops the core until a power cycle. Reset in the same extended mode
	// as a system reset request (SYSRMODE default); lt_get_reboot_reason() then
	// reports REBOOT_REASON_CRASH.
	RMU_ResetControl(rmuResetLockUp, rmuResetModeExtended);

	// DCDC first, before any clock change. On the WGM160P module the GG11's
	// DCDC output (1.8 V) also supplies the WF200's VDDRF/VDDDIG (module
	// datasheet, Figure 3.1). Out of reset the DCDC is in bypass, those rails
	// sit at VBAT and the WF200 does not answer on its host interface.
	//
	// Start from the generic EMU_DCDCINIT_DEFAULT (the WGM160PX22KGA2 branch of
	// em_emu.h is not selected by the plain EFM32GG11B device define) and apply
	// the WGM160P module values from that branch: low-noise mode in EM0,
	// analog peripherals on AVDD and 4.7 uF low-noise compensation for the
	// module's output capacitor. In the generic low-power mode the WF200 takes
	// its firmware download and then DVDD browns out (RSTCAUSE.DVDDBOD) as soon
	// as the radio firmware starts. Beyond the module default, forced-CCM
	// fast-transient reverse-current control and an 80 mA EM0 load estimate,
	// for the WF200's load steps.
	EMU_DCDCInit_TypeDef dcdcInit  = EMU_DCDCINIT_DEFAULT;
	dcdcInit.dcdcMode			   = emuDcdcMode_LowNoise;
	dcdcInit.anaPeripheralPower	   = emuDcdcAnaPeripheralPower_AVDD;
	dcdcInit.dcdcLnCompCtrl		   = emuDcdcLnCompCtrl_4u7F;
	dcdcInit.reverseCurrentControl = emuDcdcLnFastTransient;
	dcdcInit.em01LoadCurrent_mA	   = 80;
	EMU_DCDCInit(&dcdcInit);

	// Clock tree: HFXO (50 MHz) -> DPLL disciplines HFRCO to 72 MHz -> HFCLK.
	//
	// CMU_HFXOINIT_DEFAULT leaves CTUNE (crystal load capacitance) at 0, with
	// which the 50 MHz crystal never reaches HFXORDY. Pick CTUNE the way the
	// GSDK's sl_device_init_hfxo_s1.c does: the module calibration in
	// DEVINFO->MODXOCAL if valid, else the manufacturing token in the user-data
	// page, else SL_DEVICE_INIT_HFXO_CTUNE's EFM32GG default of 360.
	CMU_HFXOInit_TypeDef hfxoInit = CMU_HFXOINIT_DEFAULT;
	int32_t ctune				  = -1;
	if ((DEVINFO->MODULEINFO & _DEVINFO_MODULEINFO_HFXOCALVAL_MASK) == 0) {
		ctune = DEVINFO->MODXOCAL & _DEVINFO_MODXOCAL_HFXOCTUNE_MASK;
	}
	const uint16_t mfgCtune = *((const uint16_t *)0x0FE00100UL); // MFG token, UD page
	if (ctune == -1 && mfgCtune != 0xFFFF) {
		ctune = mfgCtune;
	}
	if (ctune == -1) {
		ctune = 360;
	}
	hfxoInit.ctuneSteadyState = (uint16_t)ctune;
	CMU_HFXOInit(&hfxoInit);

	// Bounded ready-wait instead of emlib's wait=true: an unbounded wait would
	// hang boot with no output if the crystal never starts.
	CMU_OscillatorEnable(cmuOsc_HFXO, true, false);
	for (uint32_t t = 4000000U; t && !(CMU->STATUS & CMU_STATUS_HFXORDY); t--) {}

	if (CMU->STATUS & CMU_STATUS_HFXORDY) {
		// Run from the HFXO (50 MHz) while the DPLL retunes the HFRCO.
		CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFXO);

		// 72 MHz from 50 MHz: Fout = Fref * (N+1) / (M+1) = 50 MHz * 1440 / 1000.
		// CMU_DPLLLock() requires 300 < N <= 4095 and M <= 4095. The Series 1
		// CMU_DPLLInit_TypeDef has no ditherEn; spread spectrum is off with
		// ssInterval/ssAmplitude at 0.
		CMU_DPLLInit_TypeDef dpllInit = {
			.frequency	 = 72000000U,
			.n			 = 1439U,
			.m			 = 999U,
			.ssInterval	 = 0U,
			.ssAmplitude = 0U,
			.refClk		 = cmuDPLLClkSel_Hfxo,
			.edgeSel	 = cmuDPLLEdgeSel_Fall,
			.lockMode	 = cmuDPLLLockMode_Phase,
			.autoRecover = true,
		};
		if (CMU_DPLLLock(&dpllInit)) {
			// On Series 1 the DPLL output is the HFRCO, and CMU_DPLLLock()
			// leaves the HFCLK mux alone unless HFCLK already runs from the
			// HFRCO. Select it, or the core stays on the 50 MHz HFXO.
			CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFRCO);
		}
		// else: no lock; stay on the HFXO at 50 MHz (lt_cpu_get_freq() shows it).
	} else {
		// No HFXO. On the JuiceBox 40 the GG11's HFXO never becomes ready in
		// any mode; the WF200 has its own clock and is not affected. Run the
		// HFRCO in its factory-calibrated 72 MHz band (DEVINFO->HFRCOCAL16,
		// +-1-3 % over temperature) and, when the 32.768 kHz LFXO starts,
		// discipline it with the DPLL: 32768 Hz x 2197 = 71.991 MHz (-0.012 %),
		// crystal-accurate for the UART and timekeeping (N = 2196, M = 0 are
		// within CMU_DPLLLock()'s limits).
		CMU_OscillatorEnable(cmuOsc_HFXO, false, false);
		CMU_HFRCOBandSet(cmuHFRCOFreq_72M0Hz);

		CMU_LFXOInit_TypeDef lfxoInit = CMU_LFXOINIT_DEFAULT;
		CMU_LFXOInit(&lfxoInit);
		CMU_OscillatorEnable(cmuOsc_LFXO, true, false);
		// LFXO start-up takes hundreds of ms; bounded wait of about 2 s at 72 MHz.
		for (uint32_t t = 30000000U; t && !(CMU->STATUS & CMU_STATUS_LFXORDY); t--) {}

		if (CMU->STATUS & CMU_STATUS_LFXORDY) {
			CMU_DPLLInit_TypeDef dpllInit = {
				.frequency	 = 71991296U, // 32768 * 2197
				.n			 = 2196U,
				.m			 = 0U,
				.ssInterval	 = 0U,
				.ssAmplitude = 0U,
				.refClk		 = cmuDPLLClkSel_Lfxo,
				.edgeSel	 = cmuDPLLEdgeSel_Fall,
				.lockMode	 = cmuDPLLLockMode_Phase,
				.autoRecover = true,
			};
			// Without a lock the HFRCO stays in the factory 72 MHz band.
			(void)CMU_DPLLLock(&dpllInit);
		}
	}

	CMU_ClockEnable(cmuClock_GPIO, true);

	// Bring up the console now, at the final clock, so the TX line idles high
	// from early boot and the startup banner prints at the right baud rate.
	lt_uart_init(0, PIN_SERIAL0_RX, PIN_SERIAL0_TX, NULL);
}
