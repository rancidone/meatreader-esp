#include "config_mgr.h"
#include "therm_math.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "config_mgr";

#define NVS_NAMESPACE   "meatreader"
#define NVS_KEY_CONFIG  "dev_config"
#define BLOB_VERSION    5u
#define LEGACY_BLOB_VERSION 4u
#define LEGACY_SCHEMA_VERSION 6u
#define LEGACY_NUM_CHANNELS 2

// Versioned NVS blob. Version field guards against struct layout changes.
typedef struct {
    uint32_t       version;
    device_config_t data;
} nvs_config_blob_t;

// Firmware through schema v6 supported only AIN1 and AIN2. Keep this exact
// layout so an installed device can retain its Wi-Fi settings, alerts, and
// calibration data while the two new ADS1115 inputs are added.
typedef struct {
    uint32_t schema_version;
    char  wifi_ssid[CONFIG_WIFI_SSID_MAX];
    char  wifi_password[CONFIG_WIFI_PASS_MAX];
    float sample_rate_hz;
    float ema_alpha;
    float spike_reject_delta_c;
    channel_config_t channels[LEGACY_NUM_CHANNELS];
    channel_alert_t  alerts[LEGACY_NUM_CHANNELS];
    cook_profiles_t  cook;
} legacy_device_config_t;

typedef struct {
    uint32_t               version;
    legacy_device_config_t data;
} legacy_nvs_config_blob_t;

struct config_mgr {
    SemaphoreHandle_t lock;
    device_config_t   persisted;
    device_config_t   active;
};

// ── Defaults ──────────────────────────────────────────────────────────────────

static void apply_defaults(device_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->schema_version         = CONFIG_SCHEMA_VERSION;
    strncpy(cfg->wifi_ssid,     CONFIG_WIFI_SSID,     sizeof(cfg->wifi_ssid)     - 1);
    strncpy(cfg->wifi_password, CONFIG_WIFI_PASSWORD, sizeof(cfg->wifi_password) - 1);
    cfg->sample_rate_hz         = CONFIG_DEFAULT_SAMPLE_RATE_HZ;
    cfg->ema_alpha              = CONFIG_DEFAULT_EMA_ALPHA;
    cfg->spike_reject_delta_c   = CONFIG_DEFAULT_SPIKE_REJECT_DELTA;
    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        cfg->channels[i].enabled     = true;
        cfg->channels[i].adc_channel = i;  // AIN0–AIN3, one thermistor connector per input
        cfg->channels[i].sh          = THERM_MATH_DEFAULT_COEFFS;
        snprintf(cfg->channels[i].label, CONFIG_CHANNEL_LABEL_MAX, "Channel %d", i);
        // alerts default: disabled, no target, no method
        cfg->alerts[i].enabled       = false;
        cfg->alerts[i].target_temp_c = 0.0f;
        cfg->alerts[i].method        = ALERT_METHOD_NONE;
        cfg->alerts[i].webhook_url[0] = '\0';
        cfg->alerts[i].triggered     = false;
    }
}

static void migrate_legacy_config(const legacy_device_config_t *legacy,
                                  device_config_t *out)
{
    apply_defaults(out);

    // Preserve all device-wide settings. Force NUL termination because NVS
    // data should always be treated as untrusted input.
    memcpy(out->wifi_ssid, legacy->wifi_ssid, sizeof(out->wifi_ssid));
    out->wifi_ssid[sizeof(out->wifi_ssid) - 1] = '\0';
    memcpy(out->wifi_password, legacy->wifi_password, sizeof(out->wifi_password));
    out->wifi_password[sizeof(out->wifi_password) - 1] = '\0';
    out->sample_rate_hz = legacy->sample_rate_hz;
    out->ema_alpha = legacy->ema_alpha;
    out->spike_reject_delta_c = legacy->spike_reject_delta_c;
    out->cook = legacy->cook;

    // Legacy logical channel 0 was AIN1 and channel 1 was AIN2. Match by ADC
    // input rather than by array index so their physical probe configuration
    // remains attached to the same connector after adding AIN0 and AIN3.
    for (int old_idx = 0; old_idx < LEGACY_NUM_CHANNELS; old_idx++) {
        int new_idx = -1;
        for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
            if (out->channels[i].adc_channel == legacy->channels[old_idx].adc_channel) {
                new_idx = i;
                break;
            }
        }
        if (new_idx >= 0) {
            out->channels[new_idx] = legacy->channels[old_idx];
            out->alerts[new_idx] = legacy->alerts[old_idx];
        }
    }
    out->schema_version = CONFIG_SCHEMA_VERSION;
}

// ── NVS I/O ───────────────────────────────────────────────────────────────────

static esp_err_t nvs_load_config(device_config_t *out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t size = 0;
    err = nvs_get_blob(h, NVS_KEY_CONFIG, NULL, &size);
    if (err != ESP_OK) {
        nvs_close(h);
        return ESP_ERR_NOT_FOUND;
    }

    if (size == sizeof(nvs_config_blob_t)) {
        nvs_config_blob_t blob;
        err = nvs_get_blob(h, NVS_KEY_CONFIG, &blob, &size);
        nvs_close(h);
        if (err != ESP_OK || blob.version != BLOB_VERSION) {
            return ESP_ERR_NOT_FOUND;
        }
        *out = blob.data;
    } else if (size == sizeof(legacy_nvs_config_blob_t)) {
        legacy_nvs_config_blob_t legacy;
        err = nvs_get_blob(h, NVS_KEY_CONFIG, &legacy, &size);
        nvs_close(h);
        if (err != ESP_OK || legacy.version != LEGACY_BLOB_VERSION ||
            legacy.data.schema_version != LEGACY_SCHEMA_VERSION) {
            return ESP_ERR_NOT_FOUND;
        }
        migrate_legacy_config(&legacy.data, out);
        ESP_LOGI(TAG, "Migrated saved configuration from 2 to 4 channels");
    } else {
        nvs_close(h);
        return ESP_ERR_NOT_FOUND;
    }

    // Validate S-H coefficients; replace with defaults if corrupted.
    // Also backfill label if empty (e.g. blobs written before label was added).
    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        if (!therm_math_sh_valid(&out->channels[i].sh)) {
            ESP_LOGW(TAG, "ch%d: invalid S-H coefficients in NVS, using defaults", i);
            out->channels[i].sh = THERM_MATH_DEFAULT_COEFFS;
        }
        if (out->channels[i].label[0] == '\0') {
            snprintf(out->channels[i].label, CONFIG_CHANNEL_LABEL_MAX, "Channel %d", i);
        }
    }

    return ESP_OK;
}

static esp_err_t nvs_save_config(const device_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (RW) failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_config_blob_t blob = {
        .version = BLOB_VERSION,
        .data    = *cfg,
    };
    err = nvs_set_blob(h, NVS_KEY_CONFIG, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS write failed: %s", esp_err_to_name(err));
    }
    return err;
}

// ── Public API ────────────────────────────────────────────────────────────────

esp_err_t config_mgr_init(config_mgr_t **out)
{
    struct config_mgr *mgr = calloc(1, sizeof(*mgr));
    if (!mgr) return ESP_ERR_NO_MEM;

    mgr->lock = xSemaphoreCreateMutex();
    if (!mgr->lock) {
        free(mgr);
        return ESP_ERR_NO_MEM;
    }

    apply_defaults(&mgr->persisted);

    esp_err_t err = nvs_load_config(&mgr->persisted);
    if (err == ESP_OK) {
        // Schema version check: if the loaded config was written by a different
        // firmware version the struct layout may differ.  Fall back to defaults
        // so the device doesn't boot with corrupted or misinterpreted values.
        if (mgr->persisted.schema_version != CONFIG_SCHEMA_VERSION) {
            ESP_LOGW(TAG, "NVS config schema v%lu != expected v%u — discarding, using defaults",
                     (unsigned long)mgr->persisted.schema_version, CONFIG_SCHEMA_VERSION);
            apply_defaults(&mgr->persisted);
        } else {
            // Guard: nvs_load_config() replaces the entire struct, including wifi
            // credentials.  If the stored blob has no SSID or no password (e.g.
            // config was committed before credentials were set, or from an older
            // build that didn't store them), fall back to the compiled-in menuconfig
            // defaults so the device doesn't silently lose network access.
            // A non-empty SSID+password pair was explicitly committed via the HTTP
            // API and is honoured as-is.
            if (mgr->persisted.wifi_ssid[0] == '\0' || mgr->persisted.wifi_password[0] == '\0') {
                strncpy(mgr->persisted.wifi_ssid,     CONFIG_WIFI_SSID,
                        sizeof(mgr->persisted.wifi_ssid)     - 1);
                strncpy(mgr->persisted.wifi_password, CONFIG_WIFI_PASSWORD,
                        sizeof(mgr->persisted.wifi_password) - 1);
                ESP_LOGW(TAG, "NVS config has incomplete WiFi credentials — "
                              "restored compiled-in defaults (ssid='%s')", CONFIG_WIFI_SSID);
            }
            ESP_LOGI(TAG, "Config loaded from NVS (sample_rate=%.1f Hz, ssid='%s')",
                     mgr->persisted.sample_rate_hz, mgr->persisted.wifi_ssid);
        }
    } else {
        ESP_LOGI(TAG, "No saved config found, using defaults");
    }

    mgr->active = mgr->persisted;

    *out = mgr;
    return ESP_OK;
}

void config_mgr_get_active(config_mgr_t *mgr, device_config_t *out)
{
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    *out = mgr->active;
    xSemaphoreGive(mgr->lock);
}

void config_mgr_set_active(config_mgr_t *mgr, const device_config_t *cfg)
{
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    mgr->active = *cfg;
    xSemaphoreGive(mgr->lock);
}

esp_err_t config_mgr_commit(config_mgr_t *mgr)
{
    // Copy active under lock, then write NVS outside the lock to avoid holding
    // the mutex during a potentially slow flash operation.
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    device_config_t to_save = mgr->active;
    xSemaphoreGive(mgr->lock);

    esp_err_t err = nvs_save_config(&to_save);
    if (err == ESP_OK) {
        xSemaphoreTake(mgr->lock, portMAX_DELAY);
        mgr->persisted = to_save;
        xSemaphoreGive(mgr->lock);
        ESP_LOGI(TAG, "Config committed to NVS");
    }
    return err;
}

void config_mgr_deinit(config_mgr_t *mgr)
{
    if (!mgr) return;
    if (mgr->lock) vSemaphoreDelete(mgr->lock);
    free(mgr);
}
