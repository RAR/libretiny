/* Copyright (c) Andrew Rankin 2026-06-12. */
#pragma once

#include <WiFi.h>

#include "WiFiStatusLed.h"

extern "C" {
#include "lt_dhcpserver.h"
#include "lt_wifi_reconnect.h"
#include "lt_wifi_recovery.h"
#include "lt_wifi_sta.h"
#include "sl_wfx_host_conf.h"
#include "wf200_netif.h"

#include <lwip/dns.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>

#include <sl_wfx.h>
}

// Restart the WF200 after a firmware exception (WiFiEvents.cpp,
// lt_wifi_recovery.h). With 0 the radio stays stopped until reboot.
#ifndef LT_WIFI_CHIP_RECOVERY
#define LT_WIFI_CHIP_RECOVERY 1
#endif

typedef struct {
	bool stackUp; // sl_wfx_init() and the STA netif are up
	WiFiMode mode;
	// The WF200 reported an exception: no commands until a restart clears it
	// (set and cleared by the WiFi event task).
	volatile bool chipFault;
	// Station state (status, association, address). Written only through the
	// ltWifiSta* helpers in WiFiEvents.cpp, which hold the station lock.
	lt_wifi_sta_t staState;
	// Association details for BSSID()/channel(); cleared when the link drops.
	uint8_t staBssid[6];
	uint16_t staChannel;
	SemaphoreHandle_t scanSem;
	// Scan bookkeeping (station lock): SCAN_COMPLETEs still owed by scans
	// abandoned on timeout, and whether the last scan ended without one (timeout
	// or chip fault).
	uint8_t scanStale;
	uint32_t scanStaleMs;
	volatile bool scanTimedOut;
	WiFiNetworkInfo sta;
	WiFiNetworkInfo ap;
	// softAP
	bool apUp;					  // AP netif and DHCP server exist
	volatile bool apStarted;	  // the chip reported the AP running (START_AP_IND ok, no STOP_AP_IND)
	volatile int32_t apIndStatus; // START_AP_IND status; -1 while awaited
	volatile uint8_t apClientCount;
	uint8_t apMaxClients; // 0 = chip default
	// Recursive; see ltWifiApLock().
	SemaphoreHandle_t apLock;
} WiFiData;

#define DATA  ((WiFiData *)data)
#define pDATA ((WiFiData *)pWiFi->data)
#define cDATA ((WiFiData *)cls->data)

// One driver command at a time (see lt_wfx_host_t). Only call with stackUp.
#define WIFI_CMD_TAKE() lt_wfx_cmd_lock()
#define WIFI_CMD_GIVE() lt_wfx_cmd_unlock()

// --- WiFiEvents.cpp -----------------------------------------------------------
// Host primitives, WIRQ and the event task. False if any could not be created;
// bring-up must then stop before sl_wfx_init().
bool startWifiEventTask();
// Installs the STA netif status callback; call once the netif exists.
void ltWifiAttachNetifCallback();
// Scan-result security bitmask to Arduino auth mode.
WiFiAuthMode wfxSecurityToAuthMode(uint32_t security);

// Station state, under the station lock. Each returns after posting the
// Arduino events the transition calls for.
// The host ended the attempt or link; publishes `status`.
void ltWifiStaLocalDown(WiFiStatus status);
// Associated or a join pending.
bool ltWifiStaBusy();
// Associated.
bool ltWifiStaLinkUp();
// A join was sent and the chip has not answered it yet.
bool ltWifiStaJoinPending();

// Snapshot of the stored credentials, taken under the station lock.
struct LtWifiJoinParams {
	char ssid[33];
	char password[65];
	bool open;
	uint16_t channel;
	bool hasBssid;
	uint8_t bssid[6];
};

bool ltWifiStaJoinParams(LtWifiJoinParams &params);
// Starts a join with `params` and returns once the chip has accepted it; the
// event task reports the outcome and ends the attempt if it does not reach
// WL_CONNECTED within LT_WIFI_RC_JOIN_TIMEOUT_MS. A WRONG_STATE refusal
// (the chip still leaving the previous network) is retried for up to 0.5 s.
// False if the driver refused it.
bool ltWifiSendJoin(const LtWifiJoinParams &params);
// Holds the station lock across begin()'s update of the stored credentials.
void ltWifiStaLock();
void ltWifiStaUnlock();

// STA auto-reconnect supervisor: rejoins a connection that reached
// WL_CONNECTED and was then lost, so the application need not poll status()
// (ESP32 setAutoReconnect parity, on by default). Disarmed by begin(),
// disconnect() and STA disable, so a join that never succeeded is not retried.
// A WF200 restart leaves it armed.
void ltWifiSetAutoReconnect(bool enable);
bool ltWifiGetAutoReconnect();
void ltWifiReconnectDisarm();

// Waits until cond(data) holds, at most timeoutMs, and returns cond(data).
// Gives up early after a chip fault. On the WiFi event task, which delivers
// the indications the wait is for, it handles events while it waits.
bool ltWifiWaitFor(bool (*cond)(void *data), void *data, uint32_t timeoutMs);
// Logs why the radio cannot be used while chipFault is set: a restart is
// pending, or the restarts are used up (stopped until reboot).
void ltWifiLogChipDown();

// --- WiFiAP.cpp -----------------------------------------------------------------
// Tears the softAP down (radio, DHCP server, netif); safe when it is not up.
void ltWifiApStop(void *data);
// After a WF200 restart (event task): starts the softAP again on the chip
// with the stored settings, keeping its netif and DHCP server. Tears it down
// if the chip refuses.
void ltWifiApRestore(void *data);
// Serialises the softAP's configuration and lifecycle (DATA->ap, apUp,
// apIndStatus, the AP netif and DHCP server) between the application's calls
// and the restore after a WF200 restart on the event task. Recursive: softAP()
// calls ltWifiApStop(), and an event handler run while the restore waits may
// call softAP(). Taken before the command lock and the station lock.
void ltWifiApLock(void *data);
void ltWifiApUnlock(void *data);
