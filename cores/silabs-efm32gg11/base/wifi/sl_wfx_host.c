/* Copyright (c) Andrew Rankin 2026-06-12. */
// sl_wfx_host_* hooks of the WF200 FMAC driver for FreeRTOS + lt_sdio on the
// WGM160P: SDIO bus, control GPIOs, firmware/PDS feed, locking, confirmation
// and event delivery, and the bus task that pulls frames off the chip. Secure
// Link crypto hooks are in sl_wfx_host_crypto.c.
//
// This library builds before the public LibreTiny defines exist, so
// <libretiny.h> and the LT_* log macros are unavailable here; faults are
// forwarded to the WiFi library (which logs them) as events.

#include "sl_wfx_host_conf.h"

#include "lt_sdio.h"
#include "lt_wf200_wirq.h"
#include "wf200_netif.h"

#include <sl_wfx.h>
#include <sl_wfx_host_api.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Firmware image. sl_wfx_wf200_C0.h has no include guard and defines the
// (non-static) firmware array, so it must be included in exactly one
// translation unit.
#include <sl_wfx_wf200_C0.h>

#include "sl_wfx_pds.h"

// Must match the bus layer (sl_wfx_bus_sdio.c), which rounds transfers of at
// least this many bytes up to whole SDIO blocks.
#ifndef SL_WFX_SDIO_BLOCK_MODE_THRESHOLD
#define SL_WFX_SDIO_BLOCK_MODE_THRESHOLD 0x200
#endif

// Event-queue slots kept free for state events: scan results are dropped
// rather than take the last ones, so a dense scan cannot push out a
// CONNECT/DISCONNECT indication.
#define LT_WFX_EVENT_QUEUE_LEN	   32
#define LT_WFX_EVENT_STATE_RESERVE 8

// Bus poll period while WIRQ is silent (see wfx_bus_task()).
#define LT_WFX_POLL_MS		 50
// Frames drained per wake before the bus task yields for a tick.
#define LT_WFX_DRAIN_MAX	 16
// Bound on lt_wfx_host_teardown() waiting for a data frame in flight: the
// host lock wait inside sl_wfx_send_ethernet_frame() (5 s) plus its bus
// transfer.
#define LT_WFX_TX_BARRIER_MS 6000

lt_wfx_host_t lt_wfx_host;
sl_wfx_context_t wfx_context;

// The bus task polls only between sl_wfx_enable_irq() and
// sl_wfx_disable_irq(). Earlier, during the firmware download, no frame can
// exist and a control-register poll interleaved with the download CMD53
// stream corrupts it.
static volatile bool s_rx_pollable;

// Set by a failed buffer allocation or SDIO transaction in the bus task. The
// driver's sl_wfx_receive_frame() result cannot be used for this: with Secure
// Link its error path overwrites the result with that of
// sl_wfx_secure_link_renegotiate(), normally OK, while ctrl_reg still reports
// the frame it failed to read.
static volatile bool s_rx_error;

// Base address of the RX buffer sl_wfx_receive_frame() allocated (bus task,
// under the host lock; one at a time). On Secure Link RX errors the driver
// can free at base - 4 or base + 4: the buffer pointer is advanced past the
// Secure Link header only after a successful decrypt, while the free offset
// is chosen from the encryption bitmap bit of header.id.
static void *s_rx_base;

// lt_sdio_init() has been called (it enables the controller clock and pin
// route) and lt_sdio_deinit() has not run since. sl_wfx_init()'s error path
// and lt_wfx_host_teardown() may both ask for a deinit; only the first one
// touches the controller.
static bool s_bus_up;

// The task running lt_wfx_host_teardown() and the restart after it.
static TaskHandle_t s_restart_task;

static inline bool in_bus_task(void) {
	return lt_wfx_host.bus_task != NULL && xTaskGetCurrentTaskHandle() == lt_wfx_host.bus_task;
}

static uint32_t fw_offset;

// ---- Lifecycle --------------------------------------------------------------

sl_status_t sl_wfx_host_init(void) {
	fw_offset	  = 0;
	s_rx_pollable = false;
	return SL_STATUS_OK;
}

// Also called by sl_wfx_init()'s error path, where sl_wfx_disable_irq() may
// have failed before reaching sl_wfx_host_disable_platform_interrupt().
sl_status_t sl_wfx_host_deinit(void) {
	s_rx_pollable = false;
	return SL_STATUS_OK;
}

// ---- Control GPIO -----------------------------------------------------------

sl_status_t sl_wfx_host_reset_chip(void) {
	// First host hook of a bring-up: no polling until sl_wfx_enable_irq().
	s_rx_pollable = false;
	// sl_wfx_init_bus() resets the chip before sl_wfx_host_init_bus() routes
	// the SDIO pins, so HIF_SEL would float at the RESETn edge and the chip
	// would boot in SPI mode. Drive it high here; lt_sdio_init() hands the pin
	// back to the SDIO peripheral afterwards.
	lt_wf200_pin_hif_sel_sdio();
	lt_wf200_pin_reset(true);
	sl_wfx_host_wait(10); // >= 1 ms in reset
	lt_wf200_pin_reset(false);
	sl_wfx_host_wait(25); // boot from internal ROM before the first bus access
	return SL_STATUS_OK;
}

sl_status_t sl_wfx_host_hold_in_reset(void) {
	lt_wf200_pin_reset(true);
	return SL_STATUS_OK;
}

sl_status_t sl_wfx_host_set_wake_up_pin(uint8_t state) {
	lt_wf200_pin_wup(state != 0);
	return SL_STATUS_OK;
}

sl_status_t sl_wfx_host_wait_for_wake_up(void) {
	// Power save is never enabled, so the chip is awake; a short fixed wait
	// covers the wake-up latency the driver expects.
	sl_wfx_host_wait(3);
	return SL_STATUS_OK;
}

sl_status_t sl_wfx_host_sleep_grant(
	sl_wfx_host_bus_transfer_type_t type,
	sl_wfx_register_address_t address,
	uint32_t length
) {
	(void)type;
	(void)address;
	(void)length;
	return SL_STATUS_WIFI_SLEEP_NOT_GRANTED; // WUP is held high; never sleep
}

// ---- Firmware + PDS feed ------------------------------------------------------

sl_status_t sl_wfx_host_get_firmware_size(uint32_t *firmware_size) {
	fw_offset	   = 0;
	*firmware_size = sl_wfx_firmware_size;
	return SL_STATUS_OK;
}

sl_status_t sl_wfx_host_get_firmware_data(const uint8_t **data, uint32_t data_size) {
	// The driver clamps its last block to the remaining size; check it anyway
	// so a flash over-read cannot happen.
	if (fw_offset + data_size > sl_wfx_firmware_size)
		return SL_STATUS_FAIL;
	*data = &sl_wfx_firmware[fw_offset];
	fw_offset += data_size;
	return SL_STATUS_OK;
}

sl_status_t sl_wfx_host_get_pds_size(uint16_t *pds_size) {
	// The brd4321a sl_wfx_pds.h has no key-count macro; derive it.
	*pds_size = (uint16_t)(sizeof(sl_wfx_pds) / sizeof(sl_wfx_pds[0]));
	return SL_STATUS_OK;
}

sl_status_t sl_wfx_host_get_pds_data(const char **pds_data, uint16_t index) {
	*pds_data = sl_wfx_pds[index];
	return SL_STATUS_OK;
}

// ---- OS primitives ------------------------------------------------------------

sl_status_t sl_wfx_host_wait(uint32_t wait_ms) {
	vTaskDelay(pdMS_TO_TICKS(wait_ms ? wait_ms : 1));
	return SL_STATUS_OK;
}

sl_status_t sl_wfx_host_lock(void) {
	if (xSemaphoreTake(lt_wfx_host.mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
		return SL_STATUS_TIMEOUT;
	// During a restart only the restarting task (sl_wfx_init()) and the bus
	// task may use the driver. A data frame that was held up here since
	// before the fault must not reach the new session: it would be built from
	// the old session's state, or advance the new Secure Link counters.
	if (lt_wfx_host.restarting && !in_bus_task() && xTaskGetCurrentTaskHandle() != s_restart_task) {
		xSemaphoreGive(lt_wfx_host.mutex);
		return SL_STATUS_FAIL;
	}
	return SL_STATUS_OK;
}

sl_status_t sl_wfx_host_unlock(void) {
	xSemaphoreGive(lt_wfx_host.mutex);
	return SL_STATUS_OK;
}

bool lt_wfx_cmd_lock(void) {
	if (lt_wfx_host.cmd_mutex == NULL)
		return false;
	return xSemaphoreTake(lt_wfx_host.cmd_mutex, portMAX_DELAY) == pdTRUE;
}

bool lt_wfx_cmd_lock_timeout(uint32_t timeout_ms) {
	if (lt_wfx_host.cmd_mutex == NULL)
		return false;
	return xSemaphoreTake(lt_wfx_host.cmd_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void lt_wfx_cmd_unlock(void) {
	xSemaphoreGive(lt_wfx_host.cmd_mutex);
}

sl_status_t sl_wfx_host_allocate_buffer(void **buffer, sl_wfx_buffer_type_t type, uint32_t buffer_size) {
	(void)type;
	// Every bus transfer stays inside its buffer: the bus layer rounds
	// transfers of SL_WFX_SDIO_BLOCK_MODE_THRESHOLD bytes or more up to whole
	// SDIO blocks, and lt_sdio moves byte-mode data in whole 32-bit words.
	uint32_t size;
	if (buffer_size >= SL_WFX_SDIO_BLOCK_MODE_THRESHOLD)
		size = SL_WFX_ROUND_UP(buffer_size, SL_WFX_SDIO_BLOCK_SIZE);
	else
		size = SL_WFX_ROUND_UP(buffer_size, 4);
	*buffer = pvPortMalloc(size);
	if (in_bus_task()) {
		// The bus task allocates only the RX buffer of sl_wfx_receive_frame().
		s_rx_base = *buffer;
		if (*buffer == NULL)
			s_rx_error = true;
	}
	return (*buffer != NULL) ? SL_STATUS_OK : SL_STATUS_ALLOCATION_FAILED;
}

sl_status_t sl_wfx_host_free_buffer(void *buffer, sl_wfx_buffer_type_t type) {
	(void)type;
	if (in_bus_task() && s_rx_base != NULL) {
		uint8_t *base = (uint8_t *)s_rx_base;
		uint8_t *ptr  = (uint8_t *)buffer;
		if (ptr >= base - SL_WFX_SECURE_LINK_HEADER_SIZE && ptr <= base + SL_WFX_SECURE_LINK_HEADER_SIZE)
			buffer = base; // see s_rx_base
		s_rx_base = NULL;
	}
	vPortFree(buffer);
	return SL_STATUS_OK;
}

// Driver debug output; only called when SL_WFX_DEBUG_MASK is non-zero.
void sl_wfx_host_log(const char *string, ...) {
	va_list args;
	va_start(args, string);
	vprintf(string, args);
	va_end(args);
}

// ---- SDIO bus hooks -----------------------------------------------------------
//
// Each transaction holds bus_mutex. The driver's own lock does not cover every
// bus access: sl_wfx_init() does register read-modify-writes without it after
// sl_wfx_enable_irq() has made the bus pollable, while the bus task may start
// sl_wfx_receive_frame() at any time. lt_sdio drives a single controller and
// is not reentrant, so the transactions themselves must not interleave.

sl_status_t sl_wfx_host_init_bus(void) {
	xSemaphoreTake(lt_wfx_host.bus_mutex, portMAX_DELAY);
	s_bus_up		  = true; // even if it fails part-way, the clock and route may be on
	lt_sdio_err_t err = lt_sdio_init();
	xSemaphoreGive(lt_wfx_host.bus_mutex);
	return (err == LT_SDIO_OK) ? SL_STATUS_OK : SL_STATUS_FAIL;
}

sl_status_t sl_wfx_host_deinit_bus(void) {
	s_rx_pollable = false;
	xSemaphoreTake(lt_wfx_host.bus_mutex, portMAX_DELAY);
	if (s_bus_up)
		lt_sdio_deinit();
	s_bus_up = false;
	xSemaphoreGive(lt_wfx_host.bus_mutex);
	return SL_STATUS_OK;
}

sl_status_t sl_wfx_host_sdio_transfer_cmd52(
	sl_wfx_host_bus_transfer_type_t type,
	uint8_t function,
	uint32_t address,
	uint8_t *buffer
) {
	xSemaphoreTake(lt_wfx_host.bus_mutex, portMAX_DELAY);
	lt_sdio_err_t err = lt_sdio_cmd52(type == SL_WFX_BUS_WRITE, function, address, buffer);
	xSemaphoreGive(lt_wfx_host.bus_mutex);
	if (err != LT_SDIO_OK && in_bus_task())
		s_rx_error = true;
	return (err == LT_SDIO_OK) ? SL_STATUS_OK : SL_STATUS_FAIL;
}

sl_status_t sl_wfx_host_sdio_transfer_cmd53(
	sl_wfx_host_bus_transfer_type_t type,
	uint8_t function,
	uint32_t address,
	uint8_t *buffer,
	uint16_t buffer_length
) {
	// The driver's exact length goes on the wire. Padding a byte-mode length
	// is not harmless: the WF200 ignores queue messages whose transfer length
	// disagrees with the message header (a padded PDS write is CRC-acked but
	// never confirmed). Buffers are padded by sl_wfx_host_allocate_buffer().
	xSemaphoreTake(lt_wfx_host.bus_mutex, portMAX_DELAY);
	lt_sdio_err_t err = lt_sdio_cmd53(type == SL_WFX_BUS_WRITE, function, address, buffer, buffer_length);
	xSemaphoreGive(lt_wfx_host.bus_mutex);
	if (err != LT_SDIO_OK && in_bus_task())
		s_rx_error = true;
	return (err == LT_SDIO_OK) ? SL_STATUS_OK : SL_STATUS_FAIL;
}

sl_status_t sl_wfx_host_sdio_enable_high_speed_mode(void) {
	xSemaphoreTake(lt_wfx_host.bus_mutex, portMAX_DELAY);
	lt_sdio_err_t err = lt_sdio_enable_high_speed();
	xSemaphoreGive(lt_wfx_host.bus_mutex);
	return (err == LT_SDIO_OK) ? SL_STATUS_OK : SL_STATUS_FAIL;
}

// ---- Platform interrupt (out-of-band WIRQ) -------------------------------------
//
// The driver calls these from sl_wfx_enable_irq() inside sl_wfx_init(), after
// the firmware download, and from sl_wfx_disable_irq() (sl_wfx_deinit(), and
// sl_wfx_init()'s error path). The WIRQ edge interrupt (EXTI line, NVIC,
// callback lt_wfx_host_wirq_signal()) is attached here through the base
// core's GPIO interrupt dispatcher, so it works with framework = base too.
// Attaching is idempotent, so a detach skipped because sl_wfx_disable_irq()
// failed its register access first does no harm. Edges before s_rx_pollable
// is set are ignored by the bus task.
//
// The WF200 signals in-band on DAT1 by default, which lt_sdio does not
// support. Setting SL_WFX_CONFIG_CLEAR_INT_BIT (bit 15 of CONFIG) moves the
// data IRQ to the WIRQ pin, as the GSDK host example does in
// sl_wfx_host_switch_to_wirq().

sl_status_t sl_wfx_host_enable_platform_interrupt(void) {
	// Nothing else configures the WIRQ pin; without an input mode the edge
	// detector never sees it. Pulled down, as in the GSDK host example.
	lt_wf200_pin_wirq_input();
	// Without a free EXTI line (logged) the bus task still polls every
	// LT_WFX_POLL_MS, so this costs latency, not function.
	lt_wf200_wirq_attach(lt_wfx_host_wirq_signal);
	// Without the WIRQ routing bit no interrupt ever reaches the host, so a
	// failed register access fails here rather than as a later init timeout.
	uint32_t cfg   = 0;
	sl_status_t ws = SL_STATUS_FAIL;
	if (sl_wfx_reg_read_32(SL_WFX_CONFIG_REG_ID, &cfg) == SL_STATUS_OK)
		ws = sl_wfx_reg_write_32(SL_WFX_CONFIG_REG_ID, cfg | SL_WFX_CONFIG_CLEAR_INT_BIT);
	if (ws == SL_STATUS_OK)
		s_rx_pollable = true;
	else
		lt_wf200_wirq_detach();
	return ws;
}

sl_status_t sl_wfx_host_disable_platform_interrupt(void) {
	s_rx_pollable = false; // later WIRQ edges and polls are ignored
	lt_wf200_wirq_detach();
	return SL_STATUS_OK;
}

// GPIO interrupt dispatcher callback for WIRQ (ISR context).
void lt_wfx_host_wirq_signal(void) {
	BaseType_t woken = pdFALSE;
	if (lt_wfx_host.wirq_sem)
		xSemaphoreGiveFromISR(lt_wfx_host.wirq_sem, &woken);
	portYIELD_FROM_ISR(woken);
}

// ---- Confirmation / event machinery ---------------------------------------------
//
// sl_wfx_send_request() takes sl_wfx_host_lock(), calls
// setup_waited_event(cmd_id), transmits, and unlocks before the caller enters
// wait_for_confirmation(), so a requester never blocks holding the host lock.
// sl_wfx_receive_frame() (bus task) holds the host lock across post_event()
// and frees the RX buffer afterwards, so post_event() must not block and must
// copy whatever it keeps.

sl_status_t sl_wfx_host_setup_waited_event(uint8_t event_id) {
	// Drain a give left by a confirmation that arrived after an earlier
	// wait_for_confirmation() timed out, so the next wait cannot return an old
	// payload. No give can race this: post_event() runs under the host lock,
	// and this runs either under that lock (sl_wfx_send_request) or before the
	// chip can produce frames (startup-indication setup in sl_wfx_init).
	xSemaphoreTake(lt_wfx_host.confirm_sem, 0);
	lt_wfx_host.waited_event_id = event_id;
	return SL_STATUS_OK;
}

sl_status_t sl_wfx_host_wait_for_confirmation(uint8_t confirmation_id, uint32_t timeout_ms, void **event_payload_out) {
	(void)confirmation_id; // matched in post_event()
	// A chip that raised an exception answers nothing, so fail at once
	// instead of after the timeout; post_event() gives confirm_sem when it
	// latches the fault, which wakes a requester already waiting here. The
	// result is SL_STATUS_TIMEOUT because that is what the driver's command
	// functions expect for an unanswered request (they release its input
	// buffer count on it).
	if (lt_wfx_host.chip_fault)
		return SL_STATUS_TIMEOUT;
	if (xSemaphoreTake(lt_wfx_host.confirm_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
		return SL_STATUS_TIMEOUT;
	if (lt_wfx_host.chip_fault)
		return SL_STATUS_TIMEOUT;
	if (event_payload_out != NULL)
		*event_payload_out = wfx_context.event_payload_buffer;
	return SL_STATUS_OK;
}

sl_status_t sl_wfx_host_transmit_frame(void *frame, uint32_t frame_len) {
	// Nothing reaches a chip that has raised an exception; requests fail here
	// instead of waiting for a confirmation that will not come.
	if (lt_wfx_host.chip_fault)
		return SL_STATUS_FAIL;
	return sl_wfx_data_write(frame, frame_len);
}

bool lt_wfx_host_queue_event(const lt_wfx_event_t *ev) {
	if (lt_wfx_host.event_queue == NULL)
		return false;
	if (xQueueSend(lt_wfx_host.event_queue, ev, 0) == pdTRUE)
		return true;
	bool sta = ev->id == SL_WFX_CONNECT_IND_ID || ev->id == SL_WFX_DISCONNECT_IND_ID;
	__atomic_fetch_add(sta ? &lt_wfx_host.dropped_sta : &lt_wfx_host.dropped_other, 1, __ATOMIC_RELAXED);
	return false;
}

uint32_t lt_wfx_host_take_dropped(lt_wfx_dropped_t kind) {
	volatile uint32_t *counter = kind == LT_WFX_DROPPED_STA	   ? &lt_wfx_host.dropped_sta
								 : kind == LT_WFX_DROPPED_SCAN ? &lt_wfx_host.dropped_scans
															   : &lt_wfx_host.dropped_other;
	return __atomic_exchange_n(counter, 0, __ATOMIC_RELAXED);
}

static void queue_scan_result(const sl_wfx_scan_result_ind_t *ind) {
	if (lt_wfx_host.event_queue == NULL)
		return;
	if (uxQueueSpacesAvailable(lt_wfx_host.event_queue) <= LT_WFX_EVENT_STATE_RESERVE) {
		__atomic_fetch_add(&lt_wfx_host.dropped_scans, 1, __ATOMIC_RELAXED);
		return;
	}
	lt_wfx_event_t ev = {.id = SL_WFX_SCAN_RESULT_IND_ID};
	// ssid_length is a uint32_t from the chip; clamp before use.
	uint32_t len = ind->body.ssid_def.ssid_length;
	if (len > 32)
		len = 32;
	memcpy(ev.u.scan_result.ssid, ind->body.ssid_def.ssid, len);
	ev.u.scan_result.ssid[len] = 0;
	ev.u.scan_result.ssid_len  = (uint8_t)len;
	ev.u.scan_result.channel   = ind->body.channel;
	// security_mode is a 1-byte packed bitfield (sl_wfx_security_mode_bitmask_t);
	// keep the raw byte.
	uint8_t sec = 0;
	memcpy(&sec, &ind->body.security_mode, sizeof(sec));
	ev.u.scan_result.security = sec;
	// RCPI to dBm: rcpi / 2 - 110.
	ev.u.scan_result.rssi_dbm = (int8_t)((int32_t)ind->body.rcpi / 2 - 110);
	memcpy(ev.u.scan_result.mac, ind->body.mac, 6);
	if (xQueueSend(lt_wfx_host.event_queue, &ev, 0) != pdTRUE)
		__atomic_fetch_add(&lt_wfx_host.dropped_scans, 1, __ATOMIC_RELAXED);
}

sl_status_t sl_wfx_host_post_event(sl_wfx_generic_message_t *event_payload) {
	lt_wfx_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.id = event_payload->header.id;

	switch (event_payload->header.id) {
		case SL_WFX_RECEIVED_IND_ID:
			lt_wf200_netif_input((sl_wfx_received_ind_t *)event_payload);
			break;

		case SL_WFX_CONNECT_IND_ID: {
			sl_wfx_connect_ind_t *ind = (sl_wfx_connect_ind_t *)event_payload;
			ev.u.connect.status		  = ind->body.status;
			ev.u.connect.channel	  = ind->body.channel;
			memcpy(ev.u.connect.mac, ind->body.mac, 6);
			lt_wfx_host_queue_event(&ev);
			break;
		}

		case SL_WFX_DISCONNECT_IND_ID: {
			sl_wfx_disconnect_ind_t *ind = (sl_wfx_disconnect_ind_t *)event_payload;
			ev.u.disconnect.reason		 = ind->body.reason;
			memcpy(ev.u.disconnect.mac, ind->body.mac, 6);
			lt_wfx_host_queue_event(&ev);
			break;
		}

		case SL_WFX_SCAN_RESULT_IND_ID:
			queue_scan_result((sl_wfx_scan_result_ind_t *)event_payload);
			break;

		case SL_WFX_SCAN_COMPLETE_IND_ID:
			ev.u.scan_complete.status = ((sl_wfx_scan_complete_ind_t *)event_payload)->body.status;
			lt_wfx_host_queue_event(&ev);
			break;

		case SL_WFX_START_AP_IND_ID:
			ev.u.start_ap.status = ((sl_wfx_start_ap_ind_t *)event_payload)->body.status;
			lt_wfx_host_queue_event(&ev);
			break;

		case SL_WFX_STOP_AP_IND_ID:
			lt_wfx_host_queue_event(&ev);
			break;

		case SL_WFX_AP_CLIENT_CONNECTED_IND_ID:
			memcpy(ev.u.ap_client.mac, ((sl_wfx_ap_client_connected_ind_t *)event_payload)->body.mac, 6);
			lt_wfx_host_queue_event(&ev);
			break;

		case SL_WFX_AP_CLIENT_DISCONNECTED_IND_ID: {
			sl_wfx_ap_client_disconnected_ind_t *ind = (sl_wfx_ap_client_disconnected_ind_t *)event_payload;
			ev.u.ap_client.reason					 = ind->body.reason;
			memcpy(ev.u.ap_client.mac, ind->body.mac, 6);
			lt_wfx_host_queue_event(&ev);
			break;
		}

		case SL_WFX_AP_CLIENT_REJECTED_IND_ID: {
			sl_wfx_ap_client_rejected_ind_t *ind = (sl_wfx_ap_client_rejected_ind_t *)event_payload;
			ev.u.ap_client.reason				 = ind->body.reason;
			memcpy(ev.u.ap_client.mac, ind->body.mac, 6);
			lt_wfx_host_queue_event(&ev);
			break;
		}

		case SL_WFX_ERROR_IND_ID:
			ev.u.fault.code = ((sl_wfx_error_ind_t *)event_payload)->body.type;
			lt_wfx_host_queue_event(&ev);
			break;

		case SL_WFX_EXCEPTION_IND_ID:
			// Latched here rather than only queued, so a full queue cannot
			// hide it; the WiFi library polls chip_fault. From now on the bus
			// task stops polling, data TX and new requests fail at once, and
			// the give below wakes a requester waiting for a confirmation
			// (sl_wfx_host_wait_for_confirmation() then fails).
			lt_wfx_host.fault_reason	= ((sl_wfx_exception_ind_t *)event_payload)->body.reason;
			lt_wfx_host.chip_fault		= true;
			lt_wfx_host.waited_event_id = 0;
			xSemaphoreGive(lt_wfx_host.confirm_sem);
			break;

		default:
			break;
	}

#ifdef SL_WFX_USE_SECURE_LINK
	// sl_wfx_secure_link_renegotiate_session_key() sends EXCHANGE_PUB_KEYS and
	// only then arms the wait for the matching indication, so the chip's reply
	// can arrive before the wait is set up. The driver handles that case by
	// reading the indication from wfx_context.secure_link_exchange_ind when it
	// is already filled in; store it there whenever it is not the event being
	// waited for. Without this the handshake intermittently times out and
	// sl_wfx_init() fails.
	if (event_payload->header.id == SL_WFX_SECURELINK_EXCHANGE_PUB_KEYS_IND_ID &&
		lt_wfx_host.waited_event_id != SL_WFX_SECURELINK_EXCHANGE_PUB_KEYS_IND_ID &&
		event_payload->header.length <= sizeof(wfx_context.secure_link_exchange_ind)) {
		memcpy(&wfx_context.secure_link_exchange_ind, event_payload, event_payload->header.length);
	}
#endif

	// Confirmation for the blocked requester? Clear the waited id before the
	// give so a duplicate cannot give twice (message ids are never 0). Give
	// only after a successful copy: an oversize payload makes the requester
	// time out instead of reading stale data.
	if (lt_wfx_host.waited_event_id == event_payload->header.id) {
		lt_wfx_host.waited_event_id = 0;
		if (event_payload->header.length <= sizeof(wfx_context.event_payload_buffer)) {
			memcpy(wfx_context.event_payload_buffer, event_payload, event_payload->header.length);
			xSemaphoreGive(lt_wfx_host.confirm_sem);
		}
	}
	return SL_STATUS_OK;
}

// ---- Bus task -------------------------------------------------------------------
//
// Frames are pulled off the chip by polling. A WIRQ edge wakes the task at
// once, but in practice WIRQ does not rise before sleep/PDS configuration
// (the GSDK only switches to WIRQ when power management is enabled; its
// WGM160P examples use in-band DAT1 interrupts, which lt_sdio does not
// service): the startup indication arrives with WIRQ never asserted. So every
// LT_WFX_POLL_MS the task reads the control register and drains every queued
// frame. An empty poll is one register read. RX latency is therefore up to one
// poll period, and the chip buffers whatever arrives in between.

static void wfx_bus_task(void *arg) {
	(void)arg;
	bool more = false; // frames left after the last wake's drain limit
	while (lt_wfx_host.bus_task_run) {
		// Woken by a WIRQ edge or the poll timeout; either way read the chip.
		if (!more)
			xSemaphoreTake(lt_wfx_host.wirq_sem, pdMS_TO_TICKS(LT_WFX_POLL_MS));
		more = false;
		if (!s_rx_pollable || lt_wfx_host.chip_fault)
			continue;
		// Drain until the piggy-backed control register reports no further
		// frame (ctrl_reg carries over so the driver need not re-read it), at
		// most LT_WFX_DRAIN_MAX frames, so lower-priority tasks get to run.
		// On a host-side failure back off for a poll period: retrying at once
		// would spin on the same frame at this priority.
		uint16_t ctrl_reg = 0;
		s_rx_error		  = false;
		for (unsigned n = 0; n < LT_WFX_DRAIN_MAX; n++) {
			sl_status_t result = sl_wfx_receive_frame(&ctrl_reg);
			if (s_rx_error || result != SL_STATUS_OK || (ctrl_reg & SL_WFX_CONT_NEXT_LEN_MASK) == 0)
				break;
			more = n + 1 == LT_WFX_DRAIN_MAX;
		}
		if (s_rx_error) {
			more = false;
			vTaskDelay(pdMS_TO_TICKS(LT_WFX_POLL_MS));
		} else if (more) {
			vTaskDelay(1);
		}
	}
	lt_wfx_host.bus_task = NULL;
	vTaskDelete(NULL);
}

// ---- Chip restart after a firmware exception ---------------------------------------
//
// Driver state across a restart: sl_wfx_init() clears all of wfx_context
// (buffer accounting, Secure Link keys, counters and renegotiation state)
// and re-reads the chip's MAC addresses and input-buffer count from the
// startup indication; sl_wfx_init_bus() resets the bus layer's buffer ids;
// sl_wfx_host_compute_pub_key() releases any key-exchange context left over.
// What only the host can reset is done in lt_wfx_host_teardown().

// Host state a new driver session must not inherit.
static void reset_session_state(void) {
	// The give that woke a waiting requester, if nobody took it.
	xSemaphoreTake(lt_wfx_host.confirm_sem, 0);
	lt_wfx_host.waited_event_id = 0;
	s_rx_base					= NULL;
	s_rx_error					= false;
	// Indications of the old session still queued for the WiFi library would
	// be taken for answers from the new one (a CONNECT_IND for the rejoin, a
	// START_AP_IND for the restarted AP).
	xQueueReset(lt_wfx_host.event_queue);
	lt_wfx_host.fault_reason = 0;
	lt_wfx_host.chip_fault	 = false;
}

void lt_wfx_host_prepare_init(void) {
	reset_session_state();
	s_restart_task = NULL;
	__atomic_store_n(&lt_wfx_host.restarting, false, __ATOMIC_SEQ_CST);
}

sl_status_t lt_wfx_host_chip_init(void) {
	sl_status_t st = sl_wfx_init(&wfx_context);
	if (st == SL_STATUS_OK)
		lt_wfx_host.restarting = false;
	return st;
}

sl_status_t lt_wfx_host_teardown(void) {
	// chip_fault already keeps new work off the bus: the bus task no longer
	// polls, and data frames and requests fail before reaching it.
	// `restarting` keeps other tasks out once chip_fault is cleared below. The
	// TX barrier waits out a data frame already past those checks, the host
	// lock whatever else is inside the driver (the bus task finishing the
	// frame that carried the exception); every bus wait is bounded, so these
	// are too. Commands are kept out by the command lock the caller holds.
	s_restart_task = xTaskGetCurrentTaskHandle();
	__atomic_store_n(&lt_wfx_host.restarting, true, __ATOMIC_SEQ_CST);
	if (!lt_wf200_netif_tx_barrier(LT_WFX_TX_BARRIER_MS) || sl_wfx_host_lock() != SL_STATUS_OK)
		return SL_STATUS_TIMEOUT;
	// WIRQ off and the bus down without talking to the chip: sl_wfx_deinit()
	// would send a shutdown request and register writes it no longer answers.
	// lt_sdio_deinit() also hands the SDIO pins back to GPIO, which
	// sl_wfx_host_reset_chip() needs in order to drive HIF_SEL at the reset.
	sl_wfx_host_disable_platform_interrupt();
	sl_wfx_host_deinit_bus();
	sl_wfx_host_hold_in_reset();
	sl_wfx_host_deinit();
	reset_session_state();
	sl_wfx_host_unlock();
	return SL_STATUS_OK;
}

void lt_wfx_host_hold_faulted(void) {
	lt_wfx_host.chip_fault = true;
	s_rx_pollable		   = false;
	sl_wfx_host_hold_in_reset();
}

sl_status_t lt_wfx_host_setup(void) {
	if (lt_wfx_host.mutex == NULL) {
		lt_wfx_host.mutex		= xSemaphoreCreateMutex();
		lt_wfx_host.cmd_mutex	= xSemaphoreCreateMutex();
		lt_wfx_host.bus_mutex	= xSemaphoreCreateMutex();
		lt_wfx_host.wirq_sem	= xSemaphoreCreateBinary();
		lt_wfx_host.confirm_sem = xSemaphoreCreateBinary();
		lt_wfx_host.event_queue = xQueueCreate(LT_WFX_EVENT_QUEUE_LEN, sizeof(lt_wfx_event_t));

		// The WF200 nets must be defined before the chip is reset and
		// enumerated.
		lt_wf200_pins_park();
	}
	if (!lt_wfx_host.mutex || !lt_wfx_host.cmd_mutex || !lt_wfx_host.bus_mutex || !lt_wfx_host.wirq_sem ||
		!lt_wfx_host.confirm_sem || !lt_wfx_host.event_queue)
		return SL_STATUS_ALLOCATION_FAILED;
#ifdef SL_WFX_USE_SECURE_LINK
	sl_status_t st = lt_wfx_slk_start();
	if (st != SL_STATUS_OK)
		return st;
#endif
	if (lt_wfx_host.bus_task == NULL) {
		lt_wfx_host.bus_task_run = true;
		if (xTaskCreate(wfx_bus_task, "wfx_bus", 512, NULL, configMAX_PRIORITIES - 2, &lt_wfx_host.bus_task) !=
			pdPASS) {
			lt_wfx_host.bus_task = NULL;
			return SL_STATUS_ALLOCATION_FAILED;
		}
	}
	return SL_STATUS_OK;
}
