/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "lt_sdio.h"

#include <libretiny.h>
#include <sdk_private.h>

#include <string.h>

// The controller is routed to location LOC0 (PE8-PE13), the WGM160P's internal
// WF200 bus; the variant's SDIO pins must be those.
#if PIN_WF200_SDIO_DAT3 != 0x48 || PIN_WF200_SDIO_DAT2 != 0x49 || PIN_WF200_SDIO_DAT1 != 0x4A || \
	PIN_WF200_SDIO_DAT0 != 0x4B || PIN_WF200_SDIO_CMD != 0x4C || PIN_WF200_SDIO_CLK != 0x4D
#error "lt_sdio routes SDIO to LOC0 (PE8-PE13); the variant's PIN_WF200_SDIO_* differ"
#endif

#define GPIO_PORT_PIN(gpio) LT_GPIO_PORT(gpio), LT_GPIO_PIN(gpio)

// ---- Tunables -------------------------------------------------------------
// SDIO clock = USHFRCO 50 MHz; SDHCI divisor: f = base / (2*N), N=0 -> base.
#define LT_SDIO_DIV_INIT 63U // ~397 kHz for enumeration (SD spec: <=400 kHz)
#define LT_SDIO_DIV_FS	 1U	 // 25 MHz default speed
#define LT_SDIO_DIV_HS	 0U	 // 50 MHz high speed; 1U runs high-speed timing at 25 MHz if CRC errors appear

// Software bound on every flag wait: each iteration is one AHB register read,
// about 0.5 s in all. Expiry means the controller itself is stuck.
#define LT_SDIO_WAIT_FLAG	  4000000U
// CMD5 attempts while the WF200 host interface comes up after RESETn. Each
// unanswered attempt ends in the controller's command timeout (about 0.2 ms at
// 400 kHz), so this bounds the wait to about 1 s.
#define LT_SDIO_CMD5_ATTEMPTS 5000U
#define LT_SDIO_WAIT_CMD5	  100U // CMD5 polls for I/O ready

// ---- Low-level helpers -----------------------------------------------------

static lt_sdio_err_t classify_error(uint32_t ifcr) {
	if (ifcr & (SDIO_IFCR_CMDTOUTERR | SDIO_IFCR_DATTOUTERR))
		return LT_SDIO_ERR_TIMEOUT;
	if (ifcr & (SDIO_IFCR_CMDCRCERR | SDIO_IFCR_DATCRCERR))
		return LT_SDIO_ERR_CRC;
	if (ifcr & (SDIO_IFCR_CMDINDEXERR | SDIO_IFCR_CMDENDBITERR))
		return LT_SDIO_ERR_CMD;
	return LT_SDIO_ERR_DATA;
}

// Reset the CMD/DAT state machines (SDHCI spec 3.10.1), bounded self-clear
// wait. The hardware data timeout (~2.7 s) exceeds the software wait bound,
// so a stall can end without any flag; always recover after one.
static void sdio_recover(void) {
	SDIO->CLOCKCTRL |= SDIO_CLOCKCTRL_SFTRSTCMD | SDIO_CLOCKCTRL_SFTRSTDAT;
	for (uint32_t r = LT_SDIO_WAIT_FLAG; r && (SDIO->CLOCKCTRL & (SDIO_CLOCKCTRL_SFTRSTCMD | SDIO_CLOCKCTRL_SFTRSTDAT));
		 r--) {}
}

// Wait for `mask` in IFCR; on any error flag, clear + reset CMD/DAT lines.
static lt_sdio_err_t wait_flag(uint32_t mask) {
	for (uint32_t t = LT_SDIO_WAIT_FLAG; t; t--) {
		uint32_t f = SDIO->IFCR;
		if (f & SDIO_IFCR_ERRINT) {
			lt_sdio_err_t err = classify_error(f);
			SDIO->IFCR		  = f; // write-1-to-clear everything seen
			sdio_recover();
			return err;
		}
		if (f & mask) {
			SDIO->IFCR = mask;
			return LT_SDIO_OK;
		}
	}
	sdio_recover();
	return LT_SDIO_ERR_STALL;
}

// RESPTYPESEL values (SDHCI): 0=none, 1=136-bit, 2=48-bit, 3=48-bit+busy.
#define RESP_NONE (0U << _SDIO_TFRMODE_RESPTYPESEL_SHIFT)
#define RESP_48 \
	((2U << _SDIO_TFRMODE_RESPTYPESEL_SHIFT) | SDIO_TFRMODE_CMDCRCCHKEN_ENABLE | SDIO_TFRMODE_CMDINDXCHKEN_ENABLE)
#define RESP_48BUSY \
	((3U << _SDIO_TFRMODE_RESPTYPESEL_SHIFT) | SDIO_TFRMODE_CMDCRCCHKEN_ENABLE | SDIO_TFRMODE_CMDINDXCHKEN_ENABLE)
// CMD5's R4 response has no CRC/index fields — checks must be off for it.
#define RESP_48_NOCRC (2U << _SDIO_TFRMODE_RESPTYPESEL_SHIFT)

// R5 error flags: COM_CRC_ERROR(15) | ILLEGAL_COMMAND(14) | ERROR(11) |
// FUNCTION_NUMBER(9) | OUT_OF_RANGE(8).
#define LT_SDIO_R5_ERRORS 0xCB00U

static lt_sdio_err_t sdio_command(uint8_t index, uint32_t arg, uint32_t flags) {
	uint32_t inhibit = SDIO_PRSSTAT_CMDINHIBITCMD;
	if (flags & SDIO_TFRMODE_DATPRESSEL_DATA)
		inhibit |= SDIO_PRSSTAT_CMDINHIBITDAT;
	uint32_t t;
	for (t = LT_SDIO_WAIT_FLAG; t && (SDIO->PRSSTAT & inhibit); t--) {}
	if (!t) {
		sdio_recover();
		return LT_SDIO_ERR_STALL;
	}

	SDIO->CMDARG1	  = arg;
	SDIO->TFRMODE	  = ((uint32_t)index << _SDIO_TFRMODE_CMDINDEX_SHIFT) | flags;
	lt_sdio_err_t err = wait_flag(SDIO_IFCR_CMDCOM);
	if (err != LT_SDIO_OK)
		return err;
	// R1b (48-bit + busy) asserts TRANCOM on busy release; consuming it here
	// prevents a stale flag corrupting the next CMD53's completion accounting.
	if ((flags & _SDIO_TFRMODE_RESPTYPESEL_MASK) == (3U << _SDIO_TFRMODE_RESPTYPESEL_SHIFT))
		return wait_flag(SDIO_IFCR_TRANCOM);
	return LT_SDIO_OK;
}

static lt_sdio_err_t sdio_set_clock(uint32_t divisor) {
	// Never change the clock mid-transaction: wait for CMD/DAT idle first.
	uint32_t t;
	for (t = LT_SDIO_WAIT_FLAG; t && (SDIO->PRSSTAT & (SDIO_PRSSTAT_CMDINHIBITCMD | SDIO_PRSSTAT_CMDINHIBITDAT)); t--) {
	}
	if (!t) {
		sdio_recover();
		return LT_SDIO_ERR_STALL;
	}

	SDIO->CLOCKCTRL &= ~(SDIO_CLOCKCTRL_SDCLKEN | SDIO_CLOCKCTRL_INTCLKEN);
	// 10-bit divisor: low 8 bits in SDCLKFREQSEL, upper 2 in UPPSDCLKFRE.
	uint32_t cc = SDIO->CLOCKCTRL;
	cc &= ~((0xFFU << _SDIO_CLOCKCTRL_SDCLKFREQSEL_SHIFT) | (0x3U << _SDIO_CLOCKCTRL_UPPSDCLKFRE_SHIFT));
	cc |= ((divisor & 0xFFU) << _SDIO_CLOCKCTRL_SDCLKFREQSEL_SHIFT) |
		  (((divisor >> 8) & 0x3U) << _SDIO_CLOCKCTRL_UPPSDCLKFRE_SHIFT);
	// Data timeout = max (DATTOUTCNTVAL 0xE -> 2^27 SDIO clocks).
	cc &= ~(0xFU << _SDIO_CLOCKCTRL_DATTOUTCNTVAL_SHIFT);
	cc |= (0xEU << _SDIO_CLOCKCTRL_DATTOUTCNTVAL_SHIFT);
	SDIO->CLOCKCTRL = cc | SDIO_CLOCKCTRL_INTCLKEN;
	for (t = LT_SDIO_WAIT_FLAG; t && !(SDIO->CLOCKCTRL & SDIO_CLOCKCTRL_INTCLKSTABLE); t--) {}
	if (!t)
		return LT_SDIO_ERR_STALL; // never enable SDCLK from an unstable internal clock
	SDIO->CLOCKCTRL |= SDIO_CLOCKCTRL_SDCLKEN;
	return LT_SDIO_OK;
}

// ---- Bring-up ---------------------------------------------------------------

static lt_sdio_err_t sdio_hw_setup(void) {
	// Clocks: USHFRCO 50 MHz feeds the SDIO core; HFBUS clock for registers.
	CMU_ClockEnable(cmuClock_GPIO, true);
	CMU_USHFRCOBandSet(cmuUSHFRCOFreq_50M0Hz);
	CMU_OscillatorEnable(cmuOsc_USHFRCO, true, false);
	for (uint32_t t = 4000000U; t && !(CMU->STATUS & CMU_STATUS_USHFRCORDY); t--) {}
	CMU->SDIOCTRL = (CMU->SDIOCTRL & ~_CMU_SDIOCTRL_SDIOCLKSEL_MASK) | CMU_SDIOCTRL_SDIOCLKSEL_USHFRCO;
	CMU->SDIOCTRL &= ~CMU_SDIOCTRL_SDIOCLKDIS;
	CMU->HFBUSCLKEN0 |= CMU_HFBUSCLKEN0_SDIO;

	// Full reset of the core FIRST: SFTRSTA's reset domain vs the Arasan
	// vendor-config registers (CTRL/CFG0/CFG1) is undocumented, so
	// reset-then-configure removes the question (the tap delay must survive).
	SDIO->CLOCKCTRL |= SDIO_CLOCKCTRL_SFTRSTA;
	for (uint32_t t = LT_SDIO_WAIT_FLAG; t && (SDIO->CLOCKCTRL & SDIO_CLOCKCTRL_SFTRSTA); t--) {}

	// SDIO LOC0 (DAT0=PE11, DAT1=PE10, DAT2=PE9, DAT3=PE8, CMD=PE12, CLK=PE13)
	// is the WGM160P's internal WF200 bus: PE8-13 are not bonded out as module
	// pins, while the LOC1 pins (PA0-3/PE14/PE15) are ordinary module GPIOs.
	GPIO_PinModeSet(GPIO_PORT_PIN(PIN_WF200_SDIO_DAT0), gpioModePushPull, 1);
	GPIO_PinModeSet(GPIO_PORT_PIN(PIN_WF200_SDIO_DAT1), gpioModePushPull, 1);
	GPIO_PinModeSet(GPIO_PORT_PIN(PIN_WF200_SDIO_DAT2), gpioModePushPull, 1); // also HIF_SEL
	GPIO_PinModeSet(GPIO_PORT_PIN(PIN_WF200_SDIO_DAT3), gpioModePushPull, 1);
	GPIO_PinModeSet(GPIO_PORT_PIN(PIN_WF200_SDIO_CMD), gpioModePushPull, 1);
	GPIO_PinModeSet(GPIO_PORT_PIN(PIN_WF200_SDIO_CLK), gpioModePushPull, 0);

	SDIO->ROUTELOC0 = SDIO_ROUTELOC0_DATLOC_LOC0 | SDIO_ROUTELOC0_CLKLOC_LOC0;
	SDIO->ROUTELOC1 = SDIO_ROUTELOC1_CMDLOC_LOC0;
	SDIO->ROUTEPEN	= SDIO_ROUTEPEN_CLKPEN | SDIO_ROUTEPEN_CMDPEN | SDIO_ROUTEPEN_D0PEN | SDIO_ROUTEPEN_D1PEN |
					 SDIO_ROUTEPEN_D2PEN | SDIO_ROUTEPEN_D3PEN;

	// Output tap delay 8, the value of the GSDK Micrium BSP (bsp_sd_hc.c) for
	// this module.
	SDIO->CTRL = (1U << _SDIO_CTRL_OTAPDLYEN_SHIFT) | (8U << _SDIO_CTRL_OTAPDLYSEL_SHIFT);

	// Core capability config (BSP values; declares 50 MHz base clock, 3.3 V,
	// high-speed capable). BASECLKFREQ is informational to the SDHCI core.
	SDIO->CFG0 = (0x20U << _SDIO_CFG0_TUNINGCNT_SHIFT) | (0x30U << _SDIO_CFG0_TOUTCLKFREQ_SHIFT) |
				 (1U << _SDIO_CFG0_TOUTCLKUNIT_SHIFT) | (50U << _SDIO_CFG0_BASECLKFREQ_SHIFT) |
				 (1U << _SDIO_CFG0_C8BITSUP_SHIFT) | (1U << _SDIO_CFG0_CHSSUP_SHIFT) |
				 (1U << _SDIO_CFG0_C3P3VSUP_SHIFT) | (1U << _SDIO_CFG0_C3P0VSUP_SHIFT);

	// Embedded slot: the reset value (removable slot) gates power/commands on
	// a card-detect input we never route; embedded type bypasses CD for the
	// hard-wired WF200.
	SDIO->CFG1 = SDIO_CFG1_SLOTTYPE_EMSDSLOT;

	// Internal clock at enumeration speed.
	lt_sdio_err_t err = sdio_set_clock(LT_SDIO_DIV_INIT);
	if (err != LT_SDIO_OK)
		return err;

	// Bus power: 3.3 V (SDBUSVOLTSEL=7), 1-bit width.
	SDIO->HOSTCTRL1 = (7U << _SDIO_HOSTCTRL1_SDBUSVOLTSEL_SHIFT) | SDIO_HOSTCTRL1_SDBUSPOWER;

	// Status visible in IFCR for everything; no NVIC signals (pure polling).
	SDIO->IFCR	= _SDIO_IFCR_MASK; // clear anything pending
	SDIO->IFENC = _SDIO_IFENC_MASK;
	SDIO->IEN	= 0;
	return LT_SDIO_OK;
}

lt_sdio_err_t lt_sdio_init(void) {
	lt_sdio_err_t err = sdio_hw_setup();
	if (err != LT_SDIO_OK)
		return err;

	// SD spec: >= 74 SD clocks after power/clock stable before the first
	// command (~186 us at 397 kHz) — this spin comfortably exceeds that.
	for (volatile uint32_t t = 20000U; t; t--) {}

	// CMD0: GO_IDLE. No response. SDIO-only cards may ignore it; send anyway.
	(void)sdio_command(0, 0, RESP_NONE);

	// CMD5 arg=0: read OCR (R4, no CRC). The WF200 host interface needs time
	// after RESETn before it answers, so a single CMD5 can fail on a good bus;
	// retry while the card simply does not answer (controller command
	// timeout). Any other error, including a controller stall, ends the loop
	// at once, so the total wait stays bounded.
	err = LT_SDIO_ERR_TIMEOUT;
	for (uint32_t r = LT_SDIO_CMD5_ATTEMPTS; r && err == LT_SDIO_ERR_TIMEOUT; r--) {
		err = sdio_command(5, 0, RESP_48_NOCRC);
	}
	if (err != LT_SDIO_OK)
		return err; // no card responding: WF200 absent or held in reset
	uint32_t ocr = SDIO->RESP0;

	// CMD5 with voltage window: loop until IORDY (R4 bit 31).
	uint32_t t;
	for (t = LT_SDIO_WAIT_CMD5; t; t--) {
		err = sdio_command(5, ocr & 0x00FFFF80U, RESP_48_NOCRC);
		if (err != LT_SDIO_OK)
			return err;
		if (SDIO->RESP0 & 0x80000000U)
			break;
	}
	if (!t)
		return LT_SDIO_ERR_ENUM;

	// CMD3: get RCA (R6: RCA in [31:16]).
	err = sdio_command(3, 0, RESP_48);
	if (err != LT_SDIO_OK)
		return err;
	uint32_t rca = (SDIO->RESP0 >> 16) & 0xFFFFU;

	// CMD7: select the card (R1b).
	err = sdio_command(7, rca << 16, RESP_48BUSY);
	if (err != LT_SDIO_OK)
		return err;

	// Enumeration done at 400 kHz; switch to 25 MHz default speed.
	return sdio_set_clock(LT_SDIO_DIV_FS);
}

void lt_sdio_deinit(void) {
	SDIO->CLOCKCTRL &= ~SDIO_CLOCKCTRL_SDCLKEN;
	SDIO->ROUTEPEN = 0;
	CMU->HFBUSCLKEN0 &= ~CMU_HFBUSCLKEN0_SDIO;
}

lt_sdio_err_t lt_sdio_enable_high_speed(void) {
	// SDHCI: switch bus width / HS timing with SDCLK stopped. The bus-idle
	// guard at the top of sdio_set_clock covers in-flight transactions; here
	// we are between commands (called from the FMAC HS callback), so stopping
	// the clock before flipping HOSTCTRL1 is safe.
	SDIO->CLOCKCTRL &= ~SDIO_CLOCKCTRL_SDCLKEN;
	SDIO->HOSTCTRL1 |= SDIO_HOSTCTRL1_DATTRANWD; // 4-bit (card CCCR already set)
	SDIO->HOSTCTRL1 |= SDIO_HOSTCTRL1_HSEN;
	return sdio_set_clock(LT_SDIO_DIV_HS);
}

// ---- CMD52 / CMD53 ----------------------------------------------------------

lt_sdio_err_t lt_sdio_cmd52(bool write, uint8_t function, uint32_t address, uint8_t *byte) {
	uint32_t arg = (write ? 0x80000000U : 0U) | ((uint32_t)(function & 0x7U) << 28) | ((address & 0x1FFFFU) << 9) |
				   (write ? *byte : 0U);
	lt_sdio_err_t err = sdio_command(52, arg, RESP_48);
	if (err != LT_SDIO_OK)
		return err;
	if (SDIO->RESP0 & LT_SDIO_R5_ERRORS) // card-side R5 error flags
		return LT_SDIO_ERR_CMD;
	*byte = (uint8_t)(SDIO->RESP0 & 0xFFU); // R5 read data / write echo
	return LT_SDIO_OK;
}

lt_sdio_err_t lt_sdio_cmd53(bool write, uint8_t function, uint32_t address, uint8_t *buffer, uint16_t length) {
	bool block = (length >= 512U);

	// Zero length is a caller bug (a byte-mode count of 0 means 512); a
	// block-mode length that is not a multiple of 512 cannot be transferred.
	// Byte-mode lengths go on the wire exactly (the WF200 ignores queue
	// messages whose transfer length exceeds the message length); the FIFO
	// loop moves whole words, so the last partial word is read or written in
	// full and the buffer must have room for it (see lt_sdio.h).
	if (length == 0U || (block && (length % 512U) != 0U))
		return LT_SDIO_ERR_DATA;

	uint32_t count = block ? (length / 512U) : length;

	// OP code (bit 26) = 1: incrementing address.
	uint32_t arg = (write ? 0x80000000U : 0U) | ((uint32_t)(function & 0x7U) << 28) | (block ? (1U << 27) : 0U) |
				   (1U << 26) | ((address & 0x1FFFFU) << 9) | (count & 0x1FFU);

	SDIO->BLKSIZE = ((block ? 512U : length) << _SDIO_BLKSIZE_TFRBLKSIZE_SHIFT) |
					((block ? count : 1U) << _SDIO_BLKSIZE_BLKSCNTFORCURRTFR_SHIFT);

	// DATDIRSEL_ENABLE = card-to-host (read), as in the GSDK Micrium Arasan
	// driver (sd_hc_drv_arasan.c, XFER_DIR_RD).
	uint32_t flags = RESP_48 | SDIO_TFRMODE_DATPRESSEL_DATA | (write ? 0U : SDIO_TFRMODE_DATDIRSEL_ENABLE) |
					 (block ? (SDIO_TFRMODE_BLKCNTEN_ENABLE | (1U << _SDIO_TFRMODE_MULTSINGBLKSEL_SHIFT)) : 0U);

	lt_sdio_err_t err = sdio_command(53, arg, flags);
	if (err != LT_SDIO_OK)
		return err;
	if (SDIO->RESP0 & LT_SDIO_R5_ERRORS) {
		// The card rejected the command and will not move data, but the
		// controller's data state machine is armed for it; reset it so the
		// next command does not wait on DAT inhibit.
		sdio_recover();
		return LT_SDIO_ERR_CMD;
	}

	// PIO data phase: service the FIFO one 512-byte (or `length`) chunk per
	// buffer-ready flag, 32 bits at a time.
	uint32_t remaining = length;
	uint8_t *p		   = buffer;
	while (remaining) {
		uint32_t chunk = block ? 512U : remaining;
		err			   = wait_flag(write ? SDIO_IFCR_BFRWRRDY : SDIO_IFCR_BFRRDRDY);
		if (err != LT_SDIO_OK)
			return err;
		for (uint32_t i = 0; i < chunk; i += 4) {
			if (write) {
				uint32_t w;
				memcpy(&w, p + i, 4);
				SDIO->BUFDATPORT = w;
			} else {
				uint32_t w = SDIO->BUFDATPORT;
				memcpy(p + i, &w, 4);
			}
		}
		p += chunk;
		remaining -= chunk;
	}
	return wait_flag(SDIO_IFCR_TRANCOM);
}

// ---- WF200 control pins -------------------------------------------------------
//
// Module-internal EFM32 pins wired to WF200 nets that must be held defined
// before the chip is reset: PD0-PD3 and PD5. They are absent from the WGM160P
// pin table (and from the board variant); the GSDK host example parks them the
// same way. Left floating, the WF200 never answers CMD5.
#define LT_WF200_PARK_PORT gpioPortD
#define LT_WF200_PARK_PINS 0x002FU // PD0, PD1, PD2, PD3, PD5

void lt_wf200_pins_park(void) {
	for (unsigned int pin = 0; pin < 16; pin++) {
		if (LT_WF200_PARK_PINS & (1U << pin))
			GPIO_PinModeSet(LT_WF200_PARK_PORT, pin, gpioModeDisabled, 1); // input off, weak pull-up
	}
	// Neither may float during the reset/CMD5 window. LP_CLK stays low (no
	// sleep clock); lt_wf200_pin_wup() raises WUP when the driver wants the
	// chip awake.
	GPIO_PinModeSet(GPIO_PORT_PIN(PIN_WF200_LP_CLK), gpioModePushPull, 0);
	GPIO_PinModeSet(GPIO_PORT_PIN(PIN_WF200_WUP), gpioModePushPull, 0);
}

void lt_wf200_pin_hif_sel_sdio(void) {
	GPIO_PinModeSet(GPIO_PORT_PIN(PIN_WF200_SDIO_DAT2), gpioModePushPull, 1);
}

void lt_wf200_pin_reset(bool asserted) {
	GPIO_PinModeSet(GPIO_PORT_PIN(PIN_WF200_RESET), gpioModePushPull, asserted ? 0 : 1);
}

void lt_wf200_pin_wup(bool high) {
	GPIO_PinModeSet(GPIO_PORT_PIN(PIN_WF200_WUP), gpioModePushPull, high ? 1 : 0);
}

void lt_wf200_pin_wirq_input(void) {
	GPIO_PinModeSet(GPIO_PORT_PIN(PIN_WF200_WIRQ), gpioModeInputPull, 0); // pulled down
}
