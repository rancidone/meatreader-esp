#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "therm_math.h"

#define CONFIG_NUM_CHANNELS     2
#define CONFIG_WIFI_SSID_MAX    64
#define CONFIG_WIFI_PASS_MAX    64

// Bumped whenever device_config_t layout changes. Mismatched NVS blobs are
// discarded and defaults are restored instead.
#define CONFIG_SCHEMA_VERSION   2u

// Per-channel thermistor configuration.
typedef struct {
    bool        enabled;
    int         adc_channel;   // ADS1115 channel index (0 or 1)
    sh_coeffs_t sh;            // Steinhart-Hart coefficients
} channel_config_t;

// How a temperature alert is dispatched when it fires.
typedef enum {
    ALERT_METHOD_NONE    = 0,
    ALERT_METHOD_WEBHOOK = 1,
    ALERT_METHOD_MQTT    = 2,
} alert_method_t;

// Per-channel alert configuration.
typedef struct {
    bool           enabled;
    float          target_temp_c;
    alert_method_t method;
    char           webhook_url[128];
    // Runtime state — true after alert fires; cleared by hysteresis (5°C drop).
    bool           triggered;
} channel_alert_t;

// Full device configuration. Three copies are held by config_mgr:
// persisted (NVS), active (running), staged (pending edits).
typedef struct {
    uint32_t schema_version;       // must equal CONFIG_SCHEMA_VERSION; checked on NVS load
    char  wifi_ssid[CONFIG_WIFI_SSID_MAX];
    char  wifi_password[CONFIG_WIFI_PASS_MAX];
    float sample_rate_hz;          // sensor task sample rate (0.1–10 Hz)
    float ema_alpha;               // EMA smoothing factor (0 < alpha <= 1)
    float spike_reject_delta_c;    // reject samples jumping more than this many °C from EMA
    channel_config_t channels[CONFIG_NUM_CHANNELS];
    channel_alert_t  alerts[CONFIG_NUM_CHANNELS];
} device_config_t;

#define CONFIG_DEFAULT_SAMPLE_RATE_HZ      2.0f
#define CONFIG_DEFAULT_EMA_ALPHA           0.3f
#define CONFIG_DEFAULT_SPIKE_REJECT_DELTA  5.0f

typedef struct config_mgr config_mgr_t;

// Initialize config manager. Loads from NVS; falls back to defaults on failure.
esp_err_t config_mgr_init(config_mgr_t **out);

// Thread-safe copies of each config layer.
void config_mgr_get_active(config_mgr_t *mgr, device_config_t *out);
void config_mgr_get_staged(config_mgr_t *mgr, device_config_t *out);
void config_mgr_get_persisted(config_mgr_t *mgr, device_config_t *out);

// Replace the staged config wholesale. HTTP route handlers build the new staged
// config from the current staged + JSON patch, then call this.
void config_mgr_set_staged(config_mgr_t *mgr, const device_config_t *staged);

// staged → active
void config_mgr_apply(config_mgr_t *mgr);

// active → NVS flash.  The ONLY place NVS writes occur.
esp_err_t config_mgr_commit(config_mgr_t *mgr);

// staged ← active
void config_mgr_revert_staged(config_mgr_t *mgr);

// active ← persisted, staged ← active
void config_mgr_revert_active(config_mgr_t *mgr);

void config_mgr_deinit(config_mgr_t *mgr);
