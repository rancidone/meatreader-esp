#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "therm_math.h"

#define CONFIG_NUM_CHANNELS     2
#define CONFIG_WIFI_SSID_MAX    64
#define CONFIG_WIFI_PASS_MAX    64

// Per-channel thermistor configuration.
typedef struct {
    bool        enabled;
    int         adc_channel;   // ADS1115 channel index (0 or 1)
    sh_coeffs_t sh;            // Steinhart-Hart coefficients
} channel_config_t;

// Full device configuration. Three copies are held by config_mgr:
// persisted (NVS), active (running), staged (pending edits).
typedef struct {
    char  wifi_ssid[CONFIG_WIFI_SSID_MAX];
    char  wifi_password[CONFIG_WIFI_PASS_MAX];
    float sample_rate_hz;          // sensor task sample rate (0.1–10 Hz)
    float ema_alpha;               // EMA smoothing factor (0 < alpha <= 1)
    float spike_reject_delta_c;    // reject samples jumping more than this many °C from EMA
    channel_config_t channels[CONFIG_NUM_CHANNELS];
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
