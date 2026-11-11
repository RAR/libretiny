/* Copyright (c) Andrew Rankin 2026-06-12. */
// lwIP configuration for silabs-efm32gg11 + WF200. The WF200 is a full MAC, so
// lwIP sees plain Ethernet frames. This family builds lwIP from the Gecko SDK
// copy, which has no vendor lwipopts.h, so this file is complete on its own:
// it is found first on the include path and the common LibreTiny lwipopts.h
// (which #include_next's a vendor file) is never reached. The options
// LibreTiny's common code needs from that file are repeated below.
#pragma once

#include <stdint.h>

// TCPIP_THREAD_PRIO uses configMAX_PRIORITIES, and lwIP sources do not include
// FreeRTOS headers themselves.
#include "FreeRTOSConfig.h"

// FreeRTOSConfig.h sets configENABLE_BACKWARD_COMPATIBILITY=0, which drops the
// portTICK_RATE_MS alias that the GSDK lwip-contrib FreeRTOS sys_arch.c still
// uses. It is expanded only after sys_arch.c has included FreeRTOS.h.
#ifndef portTICK_RATE_MS
#define portTICK_RATE_MS portTICK_PERIOD_MS
#endif

// --- OS mode / threading -----------------------------------------------------
#define NO_SYS										 0
#define SYS_LIGHTWEIGHT_PROT						 1
#define LWIP_TCPIP_CORE_LOCKING						 1
#define TCPIP_THREAD_NAME							 "tcpip"
#define TCPIP_THREAD_STACKSIZE						 1024 // FreeRTOS stack words (see next line)
// Without this, sys_arch.c treats the stack size as bytes and gives the tcpip
// thread a quarter of it.
#define LWIP_FREERTOS_THREAD_STACKSIZE_IS_STACKWORDS 1
#define TCPIP_THREAD_PRIO							 (configMAX_PRIORITIES - 3)
// Every received frame waiting for the tcpip thread holds one mailbox slot
// and one MEMP_TCPIP_MSG_INPKT; the mailbox also carries API calls and
// callbacks. See MEMP_NUM_TCPIP_MSG_INPKT below.
#define TCPIP_MBOX_SIZE								 32
#define DEFAULT_RAW_RECVMBOX_SIZE					 8
#define DEFAULT_UDP_RECVMBOX_SIZE					 8
#define DEFAULT_TCP_RECVMBOX_SIZE					 8
#define DEFAULT_ACCEPTMBOX_SIZE						 4

// Core-locking checks, off by default (a debugging aid): with
// -D LWIP_FREERTOS_CHECK_CORE_LOCKING=1 every raw-API call made outside the
// tcpip thread without LOCK_TCPIP_CORE() fails an lwIP assertion (which
// resets, see arch/cc.h) instead of silently racing the tcpip thread.
// Implemented by the FreeRTOS port (sys_arch.c). Not on by default because
// common LibreTiny code (LwIPmDNS.cpp) and applications (e.g. ESPHome's SNTP
// component) call the raw API from their own task, as on every other family.
#ifndef LWIP_FREERTOS_CHECK_CORE_LOCKING
#define LWIP_FREERTOS_CHECK_CORE_LOCKING 0
#endif
#if LWIP_FREERTOS_CHECK_CORE_LOCKING
#ifdef __cplusplus
extern "C" {
#endif
// sys_arch.c's lock wrappers use lock_tcpip_core without including
// lwip/tcpip.h, so declare it here (its type is completed by sys_arch.h).
struct _sys_mut;
extern struct _sys_mut lock_tcpip_core;
void sys_lock_tcpip_core(void);
void sys_unlock_tcpip_core(void);
void sys_check_core_locking(void);
void sys_mark_tcpip_thread(void);
#ifdef __cplusplus
}
#endif
#define LOCK_TCPIP_CORE()		  sys_lock_tcpip_core()
#define UNLOCK_TCPIP_CORE()		  sys_unlock_tcpip_core()
#define LWIP_ASSERT_CORE_LOCKED() sys_check_core_locking()
#define LWIP_MARK_TCPIP_THREAD()  sys_mark_tcpip_thread()
#endif

// --- APIs --------------------------------------------------------------------
#define LWIP_NETCONN		  1
#define LWIP_SOCKET			  1
// LWIP_COMPAT_SOCKETS is deliberately not defined. Common LibreTiny code needs
// both behaviours per translation unit: LwIPUdp.cpp uses the unprefixed names
// (opt.h default 1), while LwIPClient.cpp, LwIPRxBuffer.cpp and LwIPServer.cpp
// set LWIP_COMPAT_SOCKETS 0 before including <lwip/sockets.h> because their
// read/write/connect/accept methods would collide with the compat macros. A
// definition here would override that per-file opt-out.
#define LWIP_ERRNO_STDINCLUDE 1 // newlib <errno.h>; see config/arch/cc.h
#define LWIP_SO_RCVTIMEO	  1
#define LWIP_SO_SNDTIMEO	  1
#define LWIP_SO_RCVBUF		  1 // common LwIPClient: ioctl(FIONREAD)
#define LWIP_TCP_KEEPALIVE	  1

// --- Protocols -----------------------------------------------------------------
#define LWIP_ARP					  1
// The softAP DHCP server (GSDK dhcp_server.c) pins each leased client with
// etharp_add_static_entry(), which exists only with this option.
#define ETHARP_SUPPORT_STATIC_ENTRIES 1
#define LWIP_ICMP					  1
#define LWIP_DHCP					  1
#define LWIP_DNS					  1
#define LWIP_IGMP					  1
#define LWIP_IPV6					  0

// --- mDNS responder (common LwIPmDNS.cpp; as in cores/common lwipopts.h) ---------
#define LWIP_MDNS_RESPONDER			   1
#define MDNS_MAX_SERVICES			   10
#define LWIP_NUM_NETIF_CLIENT_DATA	   1 // mdns_resp_init() allocates one per netif
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1 // LwIPmDNS.cpp follows netif address changes

// --- SNTP app ----------------------------------------------------------------------
// Same time hooks as LibreTiny's common lwipopts.h: a sync sets the system
// clock, read back with time()/gettimeofday().
#define SNTP_SERVER_DNS 1
#define SNTP_SET_SYSTEM_TIME_US(sec, us)                    \
	do {                                                    \
		struct timeval tv = {.tv_sec = sec, .tv_usec = us}; \
		settimeofday(&tv, NULL);                            \
	} while (0)
#define SNTP_GET_SYSTEM_TIME(sec, us)                    \
	do {                                                 \
		struct timeval tv = {.tv_sec = 0, .tv_usec = 0}; \
		gettimeofday(&tv, NULL);                         \
		(sec) = tv.tv_sec;                               \
		(us)  = tv.tv_usec;                              \
	} while (0)

// --- Memory ----------------------------------------------------------------------
#define MEM_LIBC_MALLOC			 0
#define MEM_ALIGNMENT			 4
#define MEM_SIZE				 (48 * 1024)
#define MEMP_NUM_PBUF			 16
#define MEMP_NUM_TCP_PCB		 8
#define MEMP_NUM_TCP_PCB_LISTEN	 4
// DHCP client, DNS, SNTP, mDNS and the softAP DHCP server each hold one.
#define MEMP_NUM_UDP_PCB		 10
#define MEMP_NUM_NETCONN		 12
// lwIP's own timers (7 here) plus SNTP and one mDNS probe timer per netif.
#define MEMP_NUM_SYS_TIMEOUT	 16
// init.c refuses a TCP segment pool smaller than the send queue
// (TCP_SND_QUEUELEN defaults to 4 * TCP_SND_BUF / TCP_MSS = 32).
#define MEMP_NUM_TCP_SEG		 TCP_SND_QUEUELEN
#define PBUF_POOL_SIZE			 24
#define PBUF_POOL_BUFSIZE		 LWIP_MEM_ALIGN_SIZE(TCP_MSS + 40 + PBUF_LINK_ENCAPSULATION_HLEN + PBUF_LINK_HLEN)
// The WF200 bus task outranks the tcpip thread and drains every frame the
// chip holds on each poll (sl_wfx_host.c), so a burst arrives before tcpip
// runs. Each frame takes one pool pbuf and one input message; give every pool
// pbuf a message so the pbuf pool, not this pool, limits a burst (the lwIP
// default of 8 drops a full TCP window).
#define MEMP_NUM_TCPIP_MSG_INPKT PBUF_POOL_SIZE

// --- TCP tuning ----------------------------------------------------------------------
#define TCP_MSS		1460
#define TCP_WND		(8 * TCP_MSS)
#define TCP_SND_BUF (8 * TCP_MSS)

// --- LibreTiny common-code requirements (repeated from cores/common config) ---------
#define LWIP_TIMEVAL_PRIVATE	   0
#define LWIP_NETIF_HOSTNAME		   1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK   1

// --- Misc ------------------------------------------------------------------------------
#define LWIP_STATS 0
// TRNG-backed (base/wifi/lt_wfx_entropy.c): ephemeral ports, DHCP xid and DNS
// ids differ from boot to boot.
#ifdef __cplusplus
extern "C" {
#endif
uint32_t lt_trng_rand32(void);
#ifdef __cplusplus
}
#endif
#define LWIP_RAND() ((u32_t)lt_trng_rand32())

#include <sys/time.h>
