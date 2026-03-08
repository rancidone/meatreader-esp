#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// ── Station (STA) mode ───────────────────────────────────────────────────────

// Initialize WiFi in station (STA) mode. Call once before wifi_mgr_connect.
// Requires esp_netif_init() and esp_event_loop_create_default() already called.
esp_err_t wifi_mgr_init(void);

// Start connecting to the given AP. Non-blocking. Call wifi_mgr_wait_connected
// to block until connected, or proceed and let the device come online later.
esp_err_t wifi_mgr_connect(const char *ssid, const char *password);

// Block until connected with a valid IP, or until timeout_ms elapses.
// Returns ESP_OK if connected, ESP_ERR_TIMEOUT otherwise.
esp_err_t wifi_mgr_wait_connected(uint32_t timeout_ms);

// Returns true if currently connected and has an IP address.
bool wifi_mgr_is_connected(void);

// Returns the current IP address string (e.g. "192.168.1.100").
// Valid only while connected; returns "0.0.0.0" if not connected.
const char *wifi_mgr_get_ip(void);

void wifi_mgr_deinit(void);

// ── SoftAP (provisioning) mode ───────────────────────────────────────────────

// Start WiFi in SoftAP mode with the given SSID (open auth, no password).
// Also starts a DNS responder task that redirects all queries to 192.168.4.1
// so the captive portal popup fires on phones and laptops.
// Requires esp_netif_init() and esp_event_loop_create_default() already called.
esp_err_t wifi_mgr_start_softap(const char *ap_ssid);

// Returns true if currently running in SoftAP (provisioning) mode.
bool wifi_mgr_in_softap_mode(void);
