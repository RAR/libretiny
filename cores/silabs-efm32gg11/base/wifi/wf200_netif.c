/* Copyright (c) Andrew Rankin 2026-06-12. */
// lwIP netifs for the WF200 station and softAP interfaces. The WF200 is a full
// MAC, so lwIP exchanges plain Ethernet frames with it.
//
// Threads:
//   - RX runs in the WF200 bus task (configMAX_PRIORITIES - 2), above the lwIP
//     tcpip thread (configMAX_PRIORITIES - 3), so a flood being processed in
//     lwIP cannot stall the SDIO drain. lt_wf200_netif_input() copies each
//     frame into a pool pbuf and hands it over with tcpip_input(), which only
//     tries to post: the bus task never blocks on lwIP and drops the frame
//     when lwIP is out of pbufs or messages. One poll can deliver a burst of
//     frames before tcpip runs; lwipopts.h sizes the pbuf pool, the input
//     message pool and the tcpip mailbox for that.
//   - TX runs in the tcpip thread (linkoutput, core lock held).
//   - Everything else here takes the lwIP core lock itself and may be called
//     from any task.
//
// <libretiny.h> is not available in this library (see sl_wfx_host.c).

#include "wf200_netif.h"

#include "sl_wfx_host_conf.h"

#include <lwip/dhcp.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/prot/ethernet.h>
#include <lwip/tcpip.h>
#include <stddef.h>
#include <string.h>

#define LT_WF200_MTU 1500

static struct netif s_sta_netif;
// Named `ap_netif` and non-static because the reused GSDK dhcp_server.c binds
// its UDP socket to this object (declared in compat/app_webpage.h).
struct netif ap_netif;
static bool s_tcpip_up;
static volatile bool s_created;	  // STA netif exists
static volatile bool s_ap_active; // AP netif exists; RX for the AP is dropped otherwise

struct netif *lt_wf200_netif_sta(void) {
	return s_created ? &s_sta_netif : NULL;
}

struct netif *lt_wf200_netif_ap(void) {
	return s_ap_active ? &ap_netif : NULL;
}

// ---- TX: lwIP -> FMAC -------------------------------------------------------------
//
// sl_wfx_send_ethernet_frame() takes the driver's host lock itself and waits
// for no confirmation (the driver skips setup_waited_event for SEND_FRAME), so
// a data frame from the tcpip thread cannot disturb a command in flight and
// needs no command lock. When the chip's input buffers are full the driver
// fails fast with SL_STATUS_WOULD_OVERFLOW instead of blocking.

// Frames between the check below and the end of low_level_output_iface(),
// for lt_wf200_netif_tx_barrier().
static volatile uint32_t s_tx_busy;

static err_t send_frame(struct pbuf *p, sl_wfx_interface_t iface);

static err_t low_level_output_iface(struct netif *netif, struct pbuf *p, sl_wfx_interface_t iface) {
	(void)netif;
	// Counted before the check, so that lt_wf200_netif_tx_barrier(), which
	// sets `restarting` before reading the count, either sees this frame or
	// this frame sees `restarting`.
	__atomic_add_fetch(&s_tx_busy, 1, __ATOMIC_SEQ_CST);
	err_t err = ERR_IF;
	// The chip has stopped (EXCEPTION_IND), or is being restarted: a frame
	// written into the firmware download would corrupt it.
	if (!lt_wfx_host.chip_fault && !__atomic_load_n(&lt_wfx_host.restarting, __ATOMIC_SEQ_CST))
		err = send_frame(p, iface);
	__atomic_sub_fetch(&s_tx_busy, 1, __ATOMIC_SEQ_CST);
	return err;
}

bool lt_wf200_netif_tx_barrier(uint32_t timeout_ms) {
	for (uint32_t waited = 0; __atomic_load_n(&s_tx_busy, __ATOMIC_SEQ_CST) != 0; waited++) {
		if (waited >= timeout_ms)
			return false;
		vTaskDelay(pdMS_TO_TICKS(1));
	}
	return true;
}

static err_t send_frame(struct pbuf *p, sl_wfx_interface_t iface) {
	sl_wfx_send_frame_req_t *req = NULL;
	uint32_t frame_len			 = SL_WFX_ROUND_UP(p->tot_len, 2);

	// The driver's allocator reserves the Secure Link header, CCM padding and
	// tag around the request when SEND_FRAME is encrypted; the frame is
	// encrypted in place.
	if (sl_wfx_allocate_command_buffer(
			(sl_wfx_generic_message_t **)&req,
			SL_WFX_SEND_FRAME_REQ_ID,
			SL_WFX_TX_FRAME_BUFFER,
			sizeof(sl_wfx_send_frame_req_t) + frame_len
		) != SL_STATUS_OK)
		return ERR_MEM;
	// The buffer comes zeroed, so an odd frame's pad byte is 0.
	pbuf_copy_partial(p, req->body.packet_data, p->tot_len, 0);

	sl_status_t st = sl_wfx_send_ethernet_frame(req, frame_len, iface, 0);
	sl_wfx_free_command_buffer((sl_wfx_generic_message_t *)req, SL_WFX_SEND_FRAME_REQ_ID, SL_WFX_TX_FRAME_BUFFER);
	// A non-OK linkoutput makes TCP retry the segment from its timer rather
	// than abort the connection, so a full chip buffer is transient.
	return (st == SL_STATUS_OK) ? ERR_OK : ERR_IF;
}

static err_t low_level_output_sta(struct netif *netif, struct pbuf *p) {
	return low_level_output_iface(netif, p, SL_WFX_STA_INTERFACE);
}

static err_t low_level_output_ap(struct netif *netif, struct pbuf *p) {
	return low_level_output_iface(netif, p, SL_WFX_SOFTAP_INTERFACE);
}

// ---- RX: FMAC -> lwIP (bus task, host lock held) ------------------------------------
//
// `ind` is valid only for this call (the driver frees the RX buffer when
// post_event returns), and the host lock is held, so no driver API that takes
// it may be called here. The pbuf pools are thread-safe
// (SYS_LIGHTWEIGHT_PROT=1) and tcpip_input() only posts a pointer.

void lt_wf200_netif_input(sl_wfx_received_ind_t *ind) {
	// The interface bits in the message header select the netif; a frame for
	// an interface that is not up is dropped.
	uint8_t iface	  = (ind->header.info & SL_WFX_MSG_INFO_INTERFACE_MASK) >> SL_WFX_MSG_INFO_INTERFACE_OFFSET;
	struct netif *nif = (iface == SL_WFX_SOFTAP_INTERFACE) ? lt_wf200_netif_ap() : lt_wf200_netif_sta();
	if (nif == NULL || !netif_is_link_up(nif))
		return;

	// The frame must lie inside the message the chip sent and be a plausible
	// Ethernet frame; anything else is dropped.
	uint32_t offset = offsetof(sl_wfx_received_ind_t, body.frame) + ind->body.frame_padding;
	uint16_t len	= ind->body.frame_length;
	if (len < SIZEOF_ETH_HDR || len > SIZEOF_ETH_HDR + LT_WF200_MTU || offset + len > ind->header.length)
		return;

	struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
	if (p == NULL)
		return; // out of pool pbufs; TCP retransmits
	pbuf_take(p, (const uint8_t *)ind + offset, len);
	if (nif->input(p, nif) != ERR_OK)
		pbuf_free(p); // tcpip mailbox or message pool full
}

// ---- STA netif ----------------------------------------------------------------------

static err_t sta_netif_init(struct netif *netif) {
	netif->name[0]	  = 'w';
	netif->name[1]	  = 'f';
	netif->mtu		  = LT_WF200_MTU;
	netif->hwaddr_len = 6;
	memcpy(netif->hwaddr, wfx_context.mac_addr_0.octet, 6);
	// The WF200 forwards all multicast frames by default (no filter is
	// installed), so IGMP needs no igmp_mac_filter hook.
	netif->flags	  = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
	netif->output	  = etharp_output;
	netif->linkoutput = low_level_output_sta;
	return ERR_OK;
}

static void tcpip_done(void *arg) {
	xSemaphoreGive((SemaphoreHandle_t)arg);
}

bool lt_wf200_netif_stack_init(void) {
	if (s_tcpip_up)
		return true;
	SemaphoreHandle_t done = xSemaphoreCreateBinary();
	if (!done)
		return false;
	tcpip_init(tcpip_done, done);
	if (xSemaphoreTake(done, pdMS_TO_TICKS(5000)) != pdTRUE) {
		vSemaphoreDelete(done);
		return false;
	}
	vSemaphoreDelete(done);
	s_tcpip_up = true;
	return true;
}

// sta_netif_init() copies the MAC that sl_wfx_init() read back from the chip,
// so this must run after sl_wfx_init() succeeded.
bool lt_wf200_netif_create(void) {
	if (s_created)
		return true;
	if (!lt_wf200_netif_stack_init())
		return false;

	ip4_addr_t any;
	ip4_addr_set_zero(&any);
	LOCK_TCPIP_CORE();
	struct netif *added = netif_add(&s_sta_netif, &any, &any, &any, NULL, sta_netif_init, tcpip_input);
	if (added != NULL) {
		netif_set_default(&s_sta_netif);
		netif_set_up(&s_sta_netif);
	}
	UNLOCK_TCPIP_CORE();
	if (added == NULL)
		return false;
	s_created = true;
	return true;
}

// ---- AP netif (static address, no DHCP client) ----------------------------------------

// netif_add()'s init callback only receives the netif, so the MAC is staged
// here; ap_create is its only writer and holds the core lock across netif_add.
static uint8_t s_ap_mac[6];

static err_t ap_netif_init(struct netif *netif) {
	netif->name[0]	  = 'w';
	netif->name[1]	  = 'a';
	netif->mtu		  = LT_WF200_MTU;
	netif->hwaddr_len = 6;
	memcpy(netif->hwaddr, s_ap_mac, 6);
	netif->flags	  = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
	netif->output	  = etharp_output;
	netif->linkoutput = low_level_output_ap;
	return ERR_OK;
}

bool lt_wf200_netif_ap_create(uint32_t ip, uint32_t mask, uint32_t gw, const uint8_t mac[6]) {
	if (s_ap_active)
		return true;
	if (!s_tcpip_up)
		return false;

	ip4_addr_t ip4, mask4, gw4;
	ip4.addr   = ip;
	mask4.addr = mask;
	gw4.addr   = gw;

	LOCK_TCPIP_CORE();
	memcpy(s_ap_mac, mac, 6);
	struct netif *added = netif_add(&ap_netif, &ip4, &mask4, &gw4, NULL, ap_netif_init, tcpip_input);
	if (added != NULL) {
		netif_set_up(&ap_netif);
		netif_set_link_up(&ap_netif);
	}
	UNLOCK_TCPIP_CORE();
	if (added == NULL)
		return false;

	s_ap_active = true;
	return true;
}

// The bus task may already have picked &ap_netif in lt_wf200_netif_input()
// before s_ap_active is cleared. Delivering that one frame is harmless:
// ap_netif is static and netif_remove() only unlinks it, and lwIP drops input
// for a netif that is down.
void lt_wf200_netif_ap_destroy(void) {
	if (!s_ap_active)
		return;
	s_ap_active = false;
	LOCK_TCPIP_CORE();
	netif_set_down(&ap_netif);
	netif_remove(&ap_netif);
	UNLOCK_TCPIP_CORE();
}

void lt_wf200_netif_ap_set_link(bool up) {
	if (!s_ap_active)
		return;
	LOCK_TCPIP_CORE();
	if (up)
		netif_set_link_up(&ap_netif);
	else
		netif_set_link_down(&ap_netif);
	UNLOCK_TCPIP_CORE();
}

// ---- STA link / DHCP / address helpers (any task) -------------------------------------

void lt_wf200_netif_set_link(bool up, bool dhcp) {
	if (!s_created)
		return;
	LOCK_TCPIP_CORE();
	if (up) {
		netif_set_link_up(&s_sta_netif);
		if (dhcp)
			dhcp_start(&s_sta_netif);
	} else {
		dhcp_stop(&s_sta_netif); // no-op when DHCP is not running
		netif_set_link_down(&s_sta_netif);
	}
	UNLOCK_TCPIP_CORE();
}

void lt_wf200_netif_set_static(uint32_t ip, uint32_t mask, uint32_t gw) {
	if (!s_created)
		return;
	ip4_addr_t ip4, mask4, gw4;
	ip4.addr   = ip;
	mask4.addr = mask;
	gw4.addr   = gw;
	LOCK_TCPIP_CORE();
	// Stop DHCP first: stopping it later would clear the static address.
	dhcp_stop(&s_sta_netif);
	netif_set_addr(&s_sta_netif, &ip4, &mask4, &gw4);
	UNLOCK_TCPIP_CORE();
}

void lt_wf200_netif_set_dhcp(bool link_up) {
	if (!s_created)
		return;
	ip4_addr_t any;
	ip4_addr_set_zero(&any);
	LOCK_TCPIP_CORE();
	netif_set_addr(&s_sta_netif, &any, &any, &any);
	if (link_up)
		dhcp_start(&s_sta_netif);
	UNLOCK_TCPIP_CORE();
}

uint32_t lt_wf200_netif_sta_ip(void) {
	return s_created ? ip4_addr_get_u32(netif_ip4_addr(&s_sta_netif)) : 0;
}
