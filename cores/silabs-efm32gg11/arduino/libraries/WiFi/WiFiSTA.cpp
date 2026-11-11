/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "WiFiPrivate.h"

extern "C" {
#include <lwip/api.h> // netconn_gethostbyname
}

static inline bool radioReady(void *data) {
	return DATA->stackUp && !DATA->chipFault;
}

// Leaves the current network or join attempt. Ending the attempt before the
// command makes the DISCONNECT_IND it produces read as expected rather than
// as a lost connection, and marks the CONNECT_IND of a cancelled join as
// owed, so it is not credited to the next join (see lt_wifi_sta.h).
static void staLeave() {
	bool busy = ltWifiStaBusy();
	ltWifiStaLocalDown(WL_DISCONNECTED);
	if (busy) {
		WIFI_CMD_TAKE();
		sl_wfx_send_disconnect_command();
		WIFI_CMD_GIVE();
	}
	lt_wf200_netif_set_link(false, false);
}

static void storeString(char *&dst, const char *src) {
	if (dst == src)
		return; // restoreSTAConfig() passes our own storage back
	char *copy = src ? strdup(src) : NULL;
	free(dst);
	dst = copy;
}

WiFiStatus WiFiClass::begin(
	const char *ssid,
	const char *passphrase,
	int32_t channel,
	const uint8_t *bssid,
	bool connect
) {
	if (!enableSTA(true))
		return WL_CONNECT_FAILED;
	if (passphrase && !*passphrase)
		passphrase = NULL; // an empty passphrase means an open network
	if (!validate(ssid, passphrase))
		return WL_CONNECT_FAILED;
	if (channel < 0 || channel > 13) {
		LT_WM(WIFI, "Channel %ld not supported", (long)channel);
		return WL_CONNECT_FAILED;
	}

	// A new attempt: no auto-rejoin until it reaches WL_CONNECTED, so wrong
	// credentials are never retried in the background.
	ltWifiReconnectDisarm();
	if (radioReady(data))
		staLeave(); // the SSID may change

	WiFiNetworkInfo &info = DATA->sta;
	ltWifiStaLock();
	storeString(info.ssid, ssid);
	storeString(info.password, passphrase);
	info.channel = channel;
	if (bssid != info.bssid) {
		if (bssid) {
			if (!info.bssid)
				info.bssid = (uint8_t *)malloc(6);
			if (info.bssid)
				memcpy(info.bssid, bssid, 6);
		} else {
			free(info.bssid);
			info.bssid = NULL;
		}
	}
	bool stored = info.ssid && (!passphrase || info.password) && (!bssid || info.bssid);
	ltWifiStaUnlock();
	if (!stored)
		return WL_CONNECT_FAILED;

	if (!connect)
		return WL_DISCONNECTED;
	// Returns once the chip has accepted the join, like the other LibreTiny
	// families: WL_CONNECTED here means "join issued" (ESPHome checks for
	// it), not connected. status() reports the attempt truthfully
	// (WL_IDLE_STATUS until associated with an address), and the event task
	// posts STA_CONNECTED/GOT_IP or STA_DISCONNECTED, ending the attempt
	// after LT_WIFI_RC_JOIN_TIMEOUT_MS. While the radio is down (bring-up
	// failed, or the WF200 stopped and has not been restarted yet) the join
	// cannot be issued and this returns WL_CONNECT_FAILED; the credentials
	// stay stored for a later begin()/reconnect().
	return reconnect() ? WL_CONNECTED : WL_CONNECT_FAILED;
}

bool WiFiClass::config(IPAddress localIP, IPAddress gateway, IPAddress subnet, IPAddress dns1, IPAddress dns2) {
	if (!enableSTA(true))
		return false;
	WiFiNetworkInfo &info = DATA->sta;

	info.dns1 = dns1;
	info.dns2 = dns2;
	LOCK_TCPIP_CORE();
	if (info.dns1) {
		ip_addr_t d1 = IPADDR4_INIT(info.dns1);
		dns_setserver(0, &d1);
	}
	if (info.dns2) {
		ip_addr_t d2 = IPADDR4_INIT(info.dns2);
		dns_setserver(1, &d2);
	}
	UNLOCK_TCPIP_CORE();

	if (!localIP[0]) {
		// config(0.0.0.0, ...) reverts to DHCP (ESP32 convention).
		info.localIP = 0;
		lt_wf200_netif_set_dhcp(ltWifiStaLinkUp());
		return true;
	}
	info.localIP = localIP;
	info.subnet	 = subnet;
	info.gateway = gateway;
	// Takes effect at once; while associated the station reports WL_CONNECTED
	// with this address.
	lt_wf200_netif_set_static(info.localIP, info.subnet, info.gateway);
	return true;
}

bool WiFiClass::reconnect(const uint8_t *bssid) {
	if (!radioReady(data)) {
		if (DATA->chipFault)
			ltWifiLogChipDown();
		return false;
	}
	WiFiNetworkInfo &info = DATA->sta;
	if (bssid && bssid != info.bssid) {
		ltWifiStaLock();
		if (!info.bssid)
			info.bssid = (uint8_t *)malloc(6);
		if (info.bssid)
			memcpy(info.bssid, bssid, 6);
		ltWifiStaUnlock();
	}
	LtWifiJoinParams params;
	if (!ltWifiStaJoinParams(params))
		return false;
	if (ltWifiStaBusy())
		staLeave(); // a join while associated or joining is refused by the chip

	// Returns once the join is issued; see begin().
	LT_IM(WIFI, "Joining %s", params.ssid);
	return ltWifiSendJoin(params);
}

bool WiFiClass::disconnect(bool wifiOff) {
	// Disarm first so the supervisor cannot rejoin in between.
	ltWifiReconnectDisarm();
	bool ok = radioReady(data); // false: nothing to disconnect from
	if (ok)
		staLeave();
	if (wifiOff)
		enableSTA(false);
	return ok;
}

// The WF200 does not rejoin by itself, so the WiFi event task does: a link
// that reached WL_CONNECTED and then dropped is rejoined with backoff (ESP32
// parity; on by default).
bool WiFiClass::setAutoReconnect(bool autoReconnect) {
	ltWifiSetAutoReconnect(autoReconnect);
	return true;
}

bool WiFiClass::getAutoReconnect() {
	return ltWifiGetAutoReconnect();
}

// ---- Getters ----------------------------------------------------------------------

IPAddress WiFiClass::localIP() {
	struct netif *nif = lt_wf200_netif_sta();
	return nif ? IPAddress(netif_ip4_addr(nif)->addr) : IPAddress();
}

IPAddress WiFiClass::subnetMask() {
	struct netif *nif = lt_wf200_netif_sta();
	return nif ? IPAddress(netif_ip4_netmask(nif)->addr) : IPAddress();
}

IPAddress WiFiClass::gatewayIP() {
	struct netif *nif = lt_wf200_netif_sta();
	return nif ? IPAddress(netif_ip4_gw(nif)->addr) : IPAddress();
}

IPAddress WiFiClass::dnsIP(uint8_t dns_no) {
	const ip_addr_t *d = dns_getserver(dns_no);
	return IPAddress(ip_addr_get_ip4_u32(d));
}

IPAddress WiFiClass::broadcastIP() {
	return calculateBroadcast(localIP(), subnetMask());
}

uint8_t *WiFiClass::macAddress(uint8_t *mac) {
	// The MAC lives in the WF200 and is read by sl_wfx_init(); until the radio
	// has been brought up this returns all zeros.
	memcpy(mac, wfx_context.mac_addr_0.octet, 6);
	return mac;
}

bool WiFiClass::setMacAddress(const uint8_t *mac) {
	if (!radioReady(data) || ltWifiStaBusy())
		return false; // needs the radio up and the station idle
	sl_wfx_mac_address_t addr;
	memcpy(addr.octet, mac, 6);
	WIFI_CMD_TAKE();
	sl_status_t st = sl_wfx_set_mac_address(&addr, SL_WFX_STA_INTERFACE);
	WIFI_CMD_GIVE();
	if (st != SL_STATUS_OK)
		return false;
	struct netif *nif = lt_wf200_netif_sta();
	LOCK_TCPIP_CORE();
	memcpy(nif->hwaddr, mac, 6);
	UNLOCK_TCPIP_CORE();
	return true;
}

const String WiFiClass::SSID() {
	return DATA->sta.ssid ? String(DATA->sta.ssid) : String();
}

const String WiFiClass::psk() {
	return DATA->sta.password ? String(DATA->sta.password) : String();
}

uint8_t *WiFiClass::BSSID() {
	// From the last CONNECT_IND; zeros while not associated.
	return DATA->staBssid;
}

int32_t WiFiClass::channel() {
	return DATA->staChannel; // 0 while not associated
}

int8_t WiFiClass::RSSI() {
	if (!isConnected())
		return 0;
	uint32_t rcpi = 0;
	WIFI_CMD_TAKE();
	sl_status_t st = sl_wfx_get_signal_strength(&rcpi);
	WIFI_CMD_GIVE();
	if (st != SL_STATUS_OK)
		return 0;
	// RCPI (IEEE 802.11k): 0 = -110 dBm in 0.5 dB steps.
	return (int8_t)(((int32_t)rcpi - 220) / 2);
}

WiFiAuthMode WiFiClass::getEncryption() {
	if (!isConnected())
		return WIFI_AUTH_OPEN;
	// The join accepts WPA or WPA2 and the chip does not report which one the
	// AP chose.
	return DATA->sta.password ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;
}

const char *WiFiClass::getHostname() {
	return staHostname;
}

bool WiFiClass::setHostname(const char *hostname) {
	// Stored for the netif, which may not exist yet (it is applied at
	// bring-up); DHCP sends it from the next lease request on.
	storeHostname(staHostname, hostname);
	struct netif *nif = lt_wf200_netif_sta();
	if (nif) {
		LOCK_TCPIP_CORE();
		netif_set_hostname(nif, staHostname[0] ? staHostname : NULL);
		UNLOCK_TCPIP_CORE();
	}
	return true;
}

IPAddress WiFiClass::hostByName(const char *hostname) {
	// lwIP's blocking resolver; DNS servers come from DHCP or config().
	ip_addr_t ip;
	err_t ret = netconn_gethostbyname(hostname, &ip);
	if (ret == ERR_OK)
		return IPAddress(ip_addr_get_ip4_u32(&ip));
	return IPAddress();
}
