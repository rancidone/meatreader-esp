#include "sensor_mgr.h"
#include "ads1115.h"
#include "therm_math.h"
#include "config_mgr.h"
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "sensor_mgr";

// Sample rate bounds enforced regardless of what config says.
#define SAMPLE_RATE_MIN_HZ   0.1f   // once per 10s
#define SAMPLE_RATE_MAX_HZ   10.0f  // 10 Hz (ADS1115 at 128 SPS can support this)

// Log a summary every N ticks rather than every sample.
#define LOG_EVERY_N_TICKS    10

struct sensor_mgr {
    ads1115_handle_t   ads;
    config_mgr_t      *config;
    SemaphoreHandle_t  lock;
    sensor_snapshot_t  snapshot;
    bool               snapshot_valid;
    TaskHandle_t       task_handle;
    // Per-channel EMA state. NaN = uninitialized (seeds on first sample).
    float              ema[CONFIG_NUM_CHANNELS];
};

// ── Sampling ──────────────────────────────────────────────────────────────────

static channel_reading_t sample_channel(struct sensor_mgr *mgr, int idx,
                                         const channel_config_t *ch_cfg,
                                         float alpha, float spike_delta)
{
    channel_reading_t r = {
        .channel_id      = idx,
        .temperature_c   = 0.0f,
        .raw_adc         = 0.0f,
        .resistance_ohms = 0.0f,
        .quality         = SENSOR_QUALITY_ERROR,
    };

    if (!ch_cfg->enabled) {
        r.quality = SENSOR_QUALITY_DISABLED;
        return r;
    }

    int16_t raw = 0;
    esp_err_t err = ads1115_read_channel(mgr->ads, ch_cfg->adc_channel, &raw);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ch%d: ADS1115 read failed: %s", idx, esp_err_to_name(err));
        return r;
    }

    r.raw_adc = (float)raw;
    r.resistance_ohms = therm_math_adc_to_resistance(
        raw,
        BOARD_R_TOP_OHMS, BOARD_R_SERIES_OHMS,
        BOARD_ADS_VFS, BOARD_ADC_VREF, BOARD_ADC_MAX
    );

    if (r.resistance_ohms >= 1.0e8f) {
        ESP_LOGW(TAG, "ch%d: resistance out of range (%.0f ohms) — open circuit?",
                 idx, r.resistance_ohms);
        return r;
    }

    float raw_temp = therm_math_resistance_to_celsius(r.resistance_ohms, &ch_cfg->sh);

    // NaN propagation check (e.g. negative resistance fed to log)
    if (isnanf(raw_temp) || isinff(raw_temp)) {
        ESP_LOGW(TAG, "ch%d: temperature computation produced NaN/Inf", idx);
        return r;
    }

    // Spike rejection: skip samples that jump too far from current EMA.
    if (!isnanf(mgr->ema[idx]) && fabsf(raw_temp - mgr->ema[idx]) > spike_delta) {
        ESP_LOGD(TAG, "ch%d: spike rejected (%.1f°C, ema=%.1f°C, delta=%.1f)",
                 idx, raw_temp, mgr->ema[idx], spike_delta);
        r.temperature_c = mgr->ema[idx];
        r.quality       = SENSOR_QUALITY_OK;
        return r;
    }

    mgr->ema[idx] = therm_math_ema_update(mgr->ema[idx], raw_temp, alpha);

    r.temperature_c = mgr->ema[idx];
    r.quality       = SENSOR_QUALITY_OK;
    return r;
}

// ── Task ──────────────────────────────────────────────────────────────────────

static void sensor_task(void *pvParam)
{
    struct sensor_mgr *mgr = (struct sensor_mgr *)pvParam;
    uint32_t tick = 0;

    // Read ema_alpha from config properly on each tick
    ESP_LOGI(TAG, "Sensor task running");

    while (1) {
        device_config_t cfg;
        config_mgr_get_active(mgr->config, &cfg);

        sensor_snapshot_t snap = {
            .timestamp_ms = esp_timer_get_time() / 1000LL,
        };

        for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
            snap.channels[i] = sample_channel(mgr, i, &cfg.channels[i],
                                              cfg.ema_alpha, cfg.spike_reject_delta_c);
        }

        xSemaphoreTake(mgr->lock, portMAX_DELAY);
        mgr->snapshot       = snap;
        mgr->snapshot_valid = true;
        xSemaphoreGive(mgr->lock);

        tick++;
        if (tick % LOG_EVERY_N_TICKS == 0) {
            ESP_LOGI(TAG, "tick=%lu  ch0=%.1f°C (%s)  ch1=%.1f°C (%s)",
                     (unsigned long)tick,
                     snap.channels[0].temperature_c,
                     snap.channels[0].quality == SENSOR_QUALITY_OK ? "ok" : "err",
                     snap.channels[1].temperature_c,
                     snap.channels[1].quality == SENSOR_QUALITY_OK ? "ok" : "err");
        }

        // Clamp sample rate and compute delay.
        float rate = cfg.sample_rate_hz;
        if (rate < SAMPLE_RATE_MIN_HZ) rate = SAMPLE_RATE_MIN_HZ;
        if (rate > SAMPLE_RATE_MAX_HZ) rate = SAMPLE_RATE_MAX_HZ;
        uint32_t delay_ms = (uint32_t)(1000.0f / rate);

        // Each ADS1115 read takes ~12ms per channel, so account for that.
        // Two channels = ~24ms reading time.
        uint32_t read_time_ms = CONFIG_NUM_CHANNELS * 12;
        if (delay_ms > read_time_ms) {
            vTaskDelay(pdMS_TO_TICKS(delay_ms - read_time_ms));
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

esp_err_t sensor_mgr_init(ads1115_handle_t ads, config_mgr_t *config,
                            sensor_mgr_t **out)
{
    struct sensor_mgr *mgr = calloc(1, sizeof(*mgr));
    if (!mgr) return ESP_ERR_NO_MEM;

    mgr->ads    = ads;
    mgr->config = config;
    mgr->lock   = xSemaphoreCreateMutex();
    if (!mgr->lock) {
        free(mgr);
        return ESP_ERR_NO_MEM;
    }

    // Seed EMA to NaN so first sample initializes the filter directly.
    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        mgr->ema[i] = NAN;
    }

    *out = mgr;
    return ESP_OK;
}

esp_err_t sensor_mgr_start(sensor_mgr_t *mgr)
{
    BaseType_t ret = xTaskCreate(
        sensor_task, "sensor_mgr", 4096, mgr,
        5,   // priority: same as HTTP server default
        &mgr->task_handle
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool sensor_mgr_get_latest(sensor_mgr_t *mgr, sensor_snapshot_t *out_snap)
{
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    bool valid = mgr->snapshot_valid;
    if (valid) {
        *out_snap = mgr->snapshot;
    }
    xSemaphoreGive(mgr->lock);
    return valid;
}

void sensor_mgr_deinit(sensor_mgr_t *mgr)
{
    if (!mgr) return;
    if (mgr->task_handle) vTaskDelete(mgr->task_handle);
    if (mgr->lock)        vSemaphoreDelete(mgr->lock);
    free(mgr);
}
