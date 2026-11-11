/* Copyright (c) Andrew Rankin 2026-06-12. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// softAP DHCP server: glue over the GSDK dhcp_server.c (Apache-2.0, used
// unmodified from the Gecko SDK package). That server binds to the global
// `ap_netif`, advertises server_ip as the router (no DNS option), and leases
// <first three octets of server_ip>.10 to .17 (eight clients, one per MAC).

// server_ip and mask are network-order ip4_addr_t.addr words. Thread-safe:
// the server starts and stops on the tcpip thread.
void lt_dhcpserver_start(uint32_t server_ip, uint32_t mask);
void lt_dhcpserver_stop(void);
// Frees the lease held by a client MAC (call when the client leaves);
// without it the eight leases run out after eight distinct clients.
void lt_dhcpserver_release(const uint8_t mac[6]);
// Frees every lease, e.g. after a chip restart dropped all clients without
// telling the host.
void lt_dhcpserver_release_all(void);

#ifdef __cplusplus
}
#endif
