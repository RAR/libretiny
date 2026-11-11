/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "lt_dhcpserver.h"

#include "dhcp_server.h"
#include "lwip/def.h"
#include "lwip/tcpip.h"

#include <string.h>

// Configuration read by dhcp_server.c (declared in compat/app_webpage.h).
uint8_t ap_ip_addr0, ap_ip_addr1, ap_ip_addr2, ap_ip_addr3;
uint8_t ap_netmask_addr0, ap_netmask_addr1, ap_netmask_addr2, ap_netmask_addr3;

void lt_dhcpserver_start(uint32_t server_ip, uint32_t mask) {
	// dhcp_server.c wants dotted-quad octets, most significant first.
	uint32_t host	 = lwip_ntohl(server_ip);
	ap_ip_addr0		 = (host >> 24) & 0xFF;
	ap_ip_addr1		 = (host >> 16) & 0xFF;
	ap_ip_addr2		 = (host >> 8) & 0xFF;
	ap_ip_addr3		 = host & 0xFF;
	host			 = lwip_ntohl(mask);
	ap_netmask_addr0 = (host >> 24) & 0xFF;
	ap_netmask_addr1 = (host >> 16) & 0xFF;
	ap_netmask_addr2 = (host >> 8) & 0xFF;
	ap_netmask_addr3 = host & 0xFF;
	dhcpserver_start(); // posts to the tcpip thread
}

void lt_dhcpserver_stop(void) {
	dhcpserver_stop(); // posts to the tcpip thread
}

void lt_dhcpserver_release(const uint8_t mac[6]) {
	struct eth_addr addr;
	memcpy(addr.addr, mac, sizeof(addr.addr));
	// The lease table is also used by the server's receive callback on the
	// tcpip thread.
	LOCK_TCPIP_CORE();
	dhcpserver_remove_mac(&addr);
	UNLOCK_TCPIP_CORE();
}

void lt_dhcpserver_release_all(void) {
	LOCK_TCPIP_CORE();
	dhcpserver_clear_stored_mac();
	UNLOCK_TCPIP_CORE();
}
