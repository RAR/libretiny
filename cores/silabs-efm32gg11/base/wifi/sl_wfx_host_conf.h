/* Copyright (c) Andrew Rankin 2026-06-12. */
// Interface of the WF200 host port (sl_wfx_host.c): control pins, the event
// record handed to the Arduino WiFi library, the shared host state and the
// entry points the library calls.
#pragma once

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include <sl_wfx_constants.h>
#include <stdbool.h>
#include <stdint.h>

// The WF200 control pins come from the board variant (PIN_WF200_*) and are
// driven by the lt_wf200_pin_*() helpers in base/port/lt_sdio.c: this library
// is built without the variant header.

// Internal event ids carried in lt_wfx_event_t next to the driver's
// SL_WFX_*_IND_ID values (which are all >= 0x80, so these cannot collide).
#define LT_WFX_EVENT_IP_CHANGED 0x01 // STA netif address changed (posted by the WiFi library)
#define LT_WFX_EVENT_SLK_FAILED 0x02 // Secure Link renegotiation failed; u.fault.code = sl_status_t

// Record forwarded from sl_wfx_host_post_event() to the Arduino WiFi event
// task. Payloads are copied because the driver frees its RX buffer as soon as
// post_event returns.
typedef struct {
	uint8_t id; // SL_WFX_*_IND_ID or LT_WFX_EVENT_*

	union {
		struct {
			uint32_t status;
			uint8_t mac[6];
			uint16_t channel;
		} connect;

		struct {
			uint8_t mac[6];
			uint16_t reason;
		} disconnect;

		struct {
			uint8_t ssid[33];
			uint8_t ssid_len;
			uint8_t mac[6];
			uint16_t channel;
			int8_t rssi_dbm;
			uint32_t security;
		} scan_result;

		struct {
			uint32_t status;
		} scan_complete;

		struct {
			uint32_t status;
		} start_ap;

		struct {
			uint8_t mac[6];
			uint16_t reason; // 0 for a connect
		} ap_client;

		struct {
			uint32_t code; // error type, exception reason or sl_status_t
		} fault;
	} u;
} lt_wfx_event_t;

// Shared host-port state. The driver's confirmation machinery supports one
// command in flight (a single waited_event_id and the shared
// wfx_context.event_payload_buffer), so every sl_wfx_* command call must be
// made between lt_wfx_cmd_lock() and lt_wfx_cmd_unlock(). Data frames
// (sl_wfx_send_ethernet_frame) are exempt: they take no confirmation.
// sl_wfx_init() must be given exactly the global wfx_context.
typedef struct {
	SemaphoreHandle_t mutex;	   // sl_wfx_host_lock(): the driver's own lock
	SemaphoreHandle_t cmd_mutex;   // lt_wfx_cmd_lock(): one driver command at a time
	SemaphoreHandle_t bus_mutex;   // one SDIO transaction at a time
	SemaphoreHandle_t wirq_sem;	   // WIRQ ISR -> bus task
	SemaphoreHandle_t confirm_sem; // setup_waited_event / wait_for_confirmation
	TaskHandle_t bus_task;
	QueueHandle_t event_queue; // lt_wfx_event_t, consumed by the WiFi library
	volatile uint8_t waited_event_id;
	volatile bool bus_task_run;
	volatile bool chip_fault;		// EXCEPTION_IND seen: the chip has stopped (see lt_wfx_host_teardown())
	volatile uint32_t fault_reason; // its reason code
	// Set from lt_wfx_host_teardown() until lt_wfx_host_chip_init() succeeds:
	// only the restarting task and the bus task may enter the driver.
	volatile bool restarting;
	volatile uint32_t dropped_sta;	 // STA state events (CONNECT/DISCONNECT_IND) lost to a full queue
	volatile uint32_t dropped_other; // other events lost to a full queue
	volatile uint32_t dropped_scans; // scan results dropped to keep room for state events
} lt_wfx_host_t;

#ifdef __cplusplus
extern "C" {
#endif

extern lt_wfx_host_t lt_wfx_host;
extern sl_wfx_context_t wfx_context;

// Called from the GPIO interrupt dispatcher on a WIRQ rising edge (ISR context).
void lt_wfx_host_wirq_signal(void);
// Creates the host primitives and starts the bus task; idempotent. Must
// succeed before sl_wfx_init() is called.
sl_status_t lt_wfx_host_setup(void);
// Serialise driver commands (see lt_wfx_host_t). lt_wfx_cmd_lock() blocks
// until the lock is free and returns false only when lt_wfx_host_setup() has
// not run; lt_wfx_cmd_lock_timeout() also gives up after timeout_ms.
bool lt_wfx_cmd_lock(void);
bool lt_wfx_cmd_lock_timeout(uint32_t timeout_ms);
void lt_wfx_cmd_unlock(void);

// Before a bring-up from scratch (no driver session up; the previous
// sl_wfx_init() failed or never ran): clears what an earlier attempt left
// behind, such as a fault latched during it or the gate of a restart that
// failed, so the new attempt starts clean. Not for a session that is up:
// use lt_wfx_host_teardown() there.
void lt_wfx_host_prepare_init(void);

// sl_wfx_init(&wfx_context). After an lt_wfx_host_teardown() it must run on
// the same task, and data frames (lwIP TX) are refused until it succeeds.
// Call with the command lock held.
sl_status_t lt_wfx_host_chip_init(void);

// After a firmware exception (chip_fault): ends the driver session so that
// lt_wfx_host_chip_init() can start a fresh one. Waits (bounded) for any task
// still inside the driver, detaches WIRQ, powers the SDIO bus down, holds the
// chip in reset, discards indications queued by the old session and resets
// the host state sl_wfx_init() does not reset itself, then clears chip_fault.
// The chip is not sent anything: after an exception it no longer answers.
// Call with the command lock held, from a task that is not the bus task.
// SL_STATUS_TIMEOUT: the driver did not become free; the chip is left as it
// was, and a later call may try again.
sl_status_t lt_wfx_host_teardown(void);

// Marks the chip stopped (chip_fault) and holds it in reset, e.g. after a
// restart that failed. Polling, data TX and commands stay refused.
void lt_wfx_host_hold_faulted(void);

// Non-blocking post to the event queue from any task (not from an ISR). A
// full queue drops the event and counts it (dropped_sta or dropped_other).
bool lt_wfx_host_queue_event(const lt_wfx_event_t *ev);

typedef enum {
	LT_WFX_DROPPED_STA,
	LT_WFX_DROPPED_OTHER,
	LT_WFX_DROPPED_SCAN,
} lt_wfx_dropped_t;

// Returns and clears one of the drop counters.
uint32_t lt_wfx_host_take_dropped(lt_wfx_dropped_t kind);

#ifdef SL_WFX_USE_SECURE_LINK
// Creates the Secure Link mutex and renegotiation task (sl_wfx_host_crypto.c).
// Idempotent; called from lt_wfx_host_setup() so the objects exist before the
// key exchange inside sl_wfx_init().
sl_status_t lt_wfx_slk_start(void);
#endif

#ifdef __cplusplus
}
#endif
