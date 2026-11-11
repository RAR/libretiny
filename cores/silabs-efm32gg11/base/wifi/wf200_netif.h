/* Copyright (c) Andrew Rankin 2026-06-12. */
#pragma once

#include <sl_wfx.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct netif;

// One-time tcpip_init(). Independent of the radio, so sockets work (and fail
// cleanly) even when the WF200 does not come up. Idempotent.
bool lt_wf200_netif_stack_init(void);
// STA netif creation (runs lt_wf200_netif_stack_init() if needed). Must run
// after sl_wfx_init() succeeded (the MAC comes from the chip). Idempotent.
bool lt_wf200_netif_create(void);

// The netifs, or NULL while they do not exist. Read-only use outside the
// lwIP core lock.
struct netif *lt_wf200_netif_sta(void);
struct netif *lt_wf200_netif_ap(void);

// AP netif lifecycle (static address, no DHCP client). ip/mask/gw are
// network-order ip4_addr_t.addr words. Needs lt_wf200_netif_create() first.
// Both are idempotent; call them from one task at a time.
bool lt_wf200_netif_ap_create(uint32_t ip, uint32_t mask, uint32_t gw, const uint8_t mac[6]);
void lt_wf200_netif_ap_destroy(void);
// AP link state, for a chip restart that keeps the AP netif; no-op without one.
void lt_wf200_netif_ap_set_link(bool up);

// STA helpers; each takes the lwIP core lock, so any task may call them.
// Link up, optionally starting the DHCP client; or DHCP stopped and link down.
void lt_wf200_netif_set_link(bool up, bool dhcp);
// Static address (stops the DHCP client first).
void lt_wf200_netif_set_static(uint32_t ip, uint32_t mask, uint32_t gw);
// Clears the address and, if the link is up, starts the DHCP client.
void lt_wf200_netif_set_dhcp(bool link_up);
// Current STA IPv4 address (network order), 0 when none.
uint32_t lt_wf200_netif_sta_ip(void);

// Call after setting lt_wfx_host.restarting. Waits, at most timeout_ms, until
// no data frame that got past the check before it was set is still being
// handed to the driver; false on timeout. Such a frame holds a command
// buffer laid out for the old session's Secure Link bitmap, which must not be
// freed under the new one. Takes no lock, so any task may call it.
bool lt_wf200_netif_tx_barrier(uint32_t timeout_ms);

// RX entry, called from sl_wfx_host_post_event() in the bus task.
void lt_wf200_netif_input(sl_wfx_received_ind_t *ind);

#ifdef __cplusplus
}
#endif
