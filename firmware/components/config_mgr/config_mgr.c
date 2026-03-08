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
#define BLOB_VERSION    2u

// Versioned NVS blob. Version field guards against struct layout changes.
typedef struct {
    uint32_t       version;
    device_config_t data;
} nvs_config_blob_t;

struct config_mgr {
    SemaphoreHandle_t lock;
    device_config_t   persisted;
    device_config_t   active;
    device_config_t   staged;
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
        cfg->channels[i].adc_channel = i;
        cfg->channels[i].sh          = THERM_MATH_DEFAULT_COEFFS;
        // alerts default: disabled, no target, no method
        cfg->alerts[i].enabled      = false;
        cfg->alerts[i].target_temp_c = 0.0f;
        cfg->alerts[i].method        = ALERT_METHOD_NONE;
        cfg->alerts[i].webhook_url[0] = '\0';
        cfg->alerts[i].triggered     = false;
    }
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

    nvs_config_blob_t blob;
    size_t size = sizeof(blob);
    err = nvs_get_blob(h, NVS_KEY_CONFIG, &blob, &size);
    nvs_close(h);

    if (err != ESP_OK || size != sizeof(blob) || blob.version != BLOB_VERSION) {
        return ESP_ERR_NOT_FOUND;
    }

    // Validate S-H coefficients; replace with defaults if corrupted.
    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        if (!therm_math_sh_valid(&blob.data.channels[i].sh)) {
            ESP_LOGW(TAG, "ch%d: invalid S-H coefficients in NVS, using defaults", i);
            blob.data.channels[i].sh = THERM_MATH_DEFAULT_COEFFS;
        }
    }

    *out = blob.data;
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
    mgr->staged = mgr->active;

    *out = mgr;
    return ESP_OK;
}

void config_mgr_get_active(config_mgr_t *mgr, device_config_t *out)
{
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    *out = mgr->active;
    xSemaphoreGive(mgr->lock);
}

void config_mgr_get_staged(config_mgr_t *mgr, device_config_t *out)
{
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    *out = mgr->staged;
    xSemaphoreGive(mgr->lock);
}

void config_mgr_get_persisted(config_mgr_t *mgr, device_config_t *out)
{
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    *out = mgr->persisted;
    xSemaphoreGive(mgr->lock);
}

void config_mgr_set_staged(config_mgr_t *mgr, const device_config_t *staged)
{
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    mgr->staged = *staged;
    xSemaphoreGive(mgr->lock);
}

void config_mgr_apply(config_mgr_t *mgr)
{
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    mgr->active = mgr->staged;
    xSemaphoreGive(mgr->lock);
    ESP_LOGI(TAG, "Config applied (staged → active)");
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

void config_mgr_revert_staged(config_mgr_t *mgr)
{
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    mgr->staged = mgr->active;
    xSemaphoreGive(mgr->lock);
    ESP_LOGI(TAG, "Staged config reverted to active");
}

void config_mgr_revert_active(config_mgr_t *mgr)
{
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    mgr->active = mgr->persisted;
    mgr->staged = mgr->active;
    xSemaphoreGive(mgr->lock);
    ESP_LOGI(TAG, "Active config reverted to persisted");
}

void config_mgr_deinit(config_mgr_t *mgr)
{
    if (!mgr) return;
    if (mgr->lock) vSemaphoreDelete(mgr->lock);
    free(mgr);
}
