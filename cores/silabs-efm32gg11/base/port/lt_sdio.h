/* Copyright (c) Andrew Rankin 2026-06-12. */
// Minimal PIO-mode driver for the EFM32GG11 SDIO host controller (an SDHCI
// block), for one client: the WF200 inside the WGM160P, on SDIO route LOC0.
// No DMA, no SD/eMMC card support, no interrupts; every wait is bounded.
//
// Not reentrant: callers serialise transactions (the WF200 host port holds a
// mutex around each one).
//
// References: SD Host Controller Simplified Spec v3.00 (registers), SDIO
// Simplified Spec v3.00 (CMD5/52/53), Zephyr drivers/sdhc (command sequencing).
// The tap-delay and capability values come from the GSDK BSP for this module.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	LT_SDIO_OK = 0,
	LT_SDIO_ERR_TIMEOUT, // the card did not answer (controller-reported timeout)
	LT_SDIO_ERR_CRC,	 // command or data CRC error
	LT_SDIO_ERR_CMD,	 // command index/end-bit error, or an R5 error flag from the card
	LT_SDIO_ERR_DATA,	 // other data error, or a length the driver cannot transfer
	LT_SDIO_ERR_ENUM,	 // the card never reported I/O ready
	LT_SDIO_ERR_STALL,	 // the controller raised no completion or error flag within the software bound
} lt_sdio_err_t;

#ifdef __cplusplus
extern "C" {
#endif

// Full bring-up: clocks (USHFRCO 50 MHz), pin route, controller setup,
// 400 kHz enumeration (CMD0/CMD5/CMD3/CMD7), then 25 MHz. Leaves the bus in
// 1-bit mode; the FMAC driver sets the card's CCCR and then calls
// lt_sdio_enable_high_speed().
lt_sdio_err_t lt_sdio_init(void);
void lt_sdio_deinit(void);

// IO_RW_DIRECT. write: sends *byte and replaces it with the R5 echo.
// read: stores the value in *byte.
lt_sdio_err_t lt_sdio_cmd52(bool write, uint8_t function, uint32_t address, uint8_t *byte);

// IO_RW_EXTENDED. length >= 512 must be a multiple of 512 (block mode; the
// FMAC bus layer rounds such transfers up to whole blocks). Shorter lengths
// use byte mode and go on the wire exactly. Data moves through the FIFO in
// 32-bit words, so the buffer must have room for length rounded up to 4.
lt_sdio_err_t lt_sdio_cmd53(bool write, uint8_t function, uint32_t address, uint8_t *buffer, uint16_t length);

// Host side to 4-bit bus and high-speed clock. Call after the card's CCCR has
// been switched.
lt_sdio_err_t lt_sdio_enable_high_speed(void);

// WF200 control pins, from the board variant's PIN_WF200_* (the WF200 host
// port in base/wifi is built without the variant header, so pin handling
// lives here).
// Parks the module-internal WF200 nets and drives LP_CLK and WUP low.
void lt_wf200_pins_park(void);
// Drives SDIO_DAT2/HIF_SEL high. The WF200 samples it at the rising edge of
// RESETn (high selects SDIO), before lt_sdio_init() routes the pin.
void lt_wf200_pin_hif_sel_sdio(void);
void lt_wf200_pin_reset(bool asserted); // RESETn, active low
void lt_wf200_pin_wup(bool high);		// wake-up request
// WIRQ as a pulled-down input. Its edge interrupt is attached separately, by
// sl_wfx_host_enable_platform_interrupt() through lt_wf200_wirq_attach().
void lt_wf200_pin_wirq_input(void);

#ifdef __cplusplus
}
#endif
