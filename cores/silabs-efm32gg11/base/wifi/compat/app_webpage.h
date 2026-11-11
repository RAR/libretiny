/* Copyright (c) Andrew Rankin 2026-06-12. */
#pragma once

#include "lwip/netif.h"

// Stands in for the GSDK example's app_webpage.h, which the reused
// dhcp_server.c includes. The server only needs the AP address and netmask
// octets (defined in lt_dhcpserver.c) and the AP netif (wf200_netif.c).
extern uint8_t ap_ip_addr0, ap_ip_addr1, ap_ip_addr2, ap_ip_addr3;
extern uint8_t ap_netmask_addr0, ap_netmask_addr1, ap_netmask_addr2, ap_netmask_addr3;
extern struct netif ap_netif;
