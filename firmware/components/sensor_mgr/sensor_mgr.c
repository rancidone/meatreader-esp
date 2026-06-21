#include "sensor_mgr.h"
#include "ads1115.h"
#include "therm_math.h"
#include "config_mgr.h"
#include "i2c_manager.h"
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

static const char *TAG = "sensor_mgr";

// Sample rate bounds enforced regardless of what config says.
#define SAMPLE_RATE_MIN_HZ   0.1f   // once per 10s
#define SAMPLE_RATE_MAX_HZ   10.0f  // 10 Hz (ADS1115 at 128 SPS can support this)

// Log a summary every N ticks rather than every sample.
#define LOG_EVERY_N_TICKS    10

// Number of consecutive errors before a channel is skipped for one tick.
#define CONSEC_ERROR_SKIP_THRESHOLD  5

// History ring buffer — one point per minute, 8 bytes each.
// SENSOR_HISTORY_POINTS × 8 bytes = 2.88 KB covers 6 hours at 1-min resolution.
#define HISTORY_POINTS       SENSOR_HISTORY_POINTS
#define HISTORY_INTERVAL_MS  60000  // push one point every 60 s

typedef struct {
    int32_t  timestamp_s;                     // Unix epoch seconds (approx: boot_epoch + uptime)
    int16_t  temp_x10[CONFIG_NUM_CHANNELS];   // °C × 10 (int16 range: ±3276.7°C, enough)
} history_point_t;
// After this many all-channel-error cycles, reset the I2C bus to recover
// from a stuck SDA/SCL line before the watchdog would fire.
#define I2C_BUS_RESET_ERROR_CYCLES   3
// Repeated warning logs are emitted on first occurrence, then every Nth repeat.
#define WARN_REPEAT_EVERY            30
// ADC stabilization: discard one conversion after each mux switch, then
// average a small number of samples to reduce quantization and pickup noise.
#define ADS_SETTLE_DISCARD_SAMPLES   1
#define ADS_OVERSAMPLE_SAMPLES       2
// Open probe detection. Floating/open thermistor inputs pull up toward VCC
// through R_TOP, typically landing at 3.1–3.3 V (raw ~24k–26k, resistance
// ~200k–700k Ω). The raw threshold catches readings above ~2.75 V; the
// resistance threshold catches any remaining high-resistance cases.
// Both are well above the ~100 kΩ maximum for this thermistor at –20 °C.
// Physical max from this circuit is ~26384 (Vcc=3.3V, VFS=4.096V).
// Setting threshold above that so cold-probe readings (~25k raw at 0°F) aren't
// falsely rejected here; the resistance check below handles true open circuits.
#define OPEN_CIRCUIT_RAW_THRESHOLD   26500
#define OPEN_CIRCUIT_RES_OHMS        500000.0f

struct sensor_mgr {
    ads1115_handle_t   ads;
    config_mgr_t      *config;
    SemaphoreHandle_t  lock;
    sensor_snapshot_t  snapshot;
    bool               snapshot_valid;
    TaskHandle_t       task_handle;
    EventGroupHandle_t snapshot_event_group;
    // Per-channel EMA state. NaN = uninitialized (seeds on first sample).
    float              ema[CONFIG_NUM_CHANNELS];
    // Counts consecutive cycles where every active channel errored.
    uint8_t            all_error_cycles;
    // Per-channel consecutive error counter for hardware robustness.
    uint8_t            consecutive_errors[CONFIG_NUM_CHANNELS];
    // Per-channel warning repeat counters to prevent log spam.
    uint16_t           warn_repeat_counts[CONFIG_NUM_CHANNELS][5];
    // History ring buffer (one point/minute, downsampled from EMA).
    history_point_t    history[HISTORY_POINTS];
    int                history_head;    // next write index (ring buffer)
    int                history_count;   // number of valid entries (≤ HISTORY_POINTS)
    int64_t            history_next_ms; // boot-time ms when next point should be pushed
    // Per-channel accumulator for 1-minute downsampled average.
    float              hist_acc[CONFIG_NUM_CHANNELS];
    int                hist_acc_n[CONFIG_NUM_CHANNELS];
};

typedef enum {
    SENSOR_WARN_READ_FAIL = 0,
    SENSOR_WARN_OPEN_RAW,
    SENSOR_WARN_OPEN_OHMS,
    SENSOR_WARN_TEMP_NAN,
    SENSOR_WARN_SKIP,
} sensor_warn_t;

static uint16_t bump_warn_count(struct sensor_mgr *mgr, int idx, sensor_warn_t warn)
{
    if (mgr->warn_repeat_counts[idx][warn] < UINT16_MAX) {
        mgr->warn_repeat_counts[idx][warn]++;
    }
    return mgr->warn_repeat_counts[idx][warn];
}

static bool should_emit_warn(uint16_t count)
{
    return count == 1 || (count % WARN_REPEAT_EVERY) == 0;
}

// ── Sampling ──────────────────────────────────────────────────────────────────

static esp_err_t read_channel_smoothed(struct sensor_mgr *mgr, int adc_channel,
                                       int16_t *out_raw)
{
    const int total_reads = ADS_SETTLE_DISCARD_SAMPLES + ADS_OVERSAMPLE_SAMPLES;
    int32_t sum = 0;
    int kept = 0;

    for (int i = 0; i < total_reads; i++) {
        int16_t raw = 0;
        esp_err_t err = ads1115_read_channel(mgr->ads, adc_channel, &raw);
        if (err != ESP_OK) return err;

        if (i < ADS_SETTLE_DISCARD_SAMPLES) {
            continue;
        }
        sum += raw;
        kept++;
    }

    if (kept <= 0) return ESP_FAIL;
    *out_raw = (int16_t)(sum / kept);
    return ESP_OK;
}

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

    // Bounds check: ADS1115 has 4 single-ended channels (0–3).
    if (ch_cfg->adc_channel > 3) {
        ESP_LOGE(TAG, "ch%d: invalid adc_channel %u (must be 0–3)", idx, ch_cfg->adc_channel);
        return r;  // quality = SENSOR_QUALITY_ERROR
    }

    int16_t raw = 0;
    esp_err_t err = read_channel_smoothed(mgr, ch_cfg->adc_channel, &raw);
    if (err != ESP_OK) {
        uint16_t count = bump_warn_count(mgr, idx, SENSOR_WARN_READ_FAIL);
        if (should_emit_warn(count)) {
            ESP_LOGW(TAG, "ch%d: ADS1115 read failed: %s%s", idx, esp_err_to_name(err),
                     count > 1 ? " (repeating)" : "");
        }
        return r;
    }
    if (raw >= OPEN_CIRCUIT_RAW_THRESHOLD) {
        uint16_t count = bump_warn_count(mgr, idx, SENSOR_WARN_OPEN_RAW);
        if (should_emit_warn(count)) {
            ESP_LOGW(TAG, "ch%d: likely open circuit (raw=%d)%s", idx, raw,
                     count > 1 ? " (repeating)" : "");
        }
        return r;
    }

    r.raw_adc = (float)raw;
    r.resistance_ohms = therm_math_adc_to_resistance(
        raw,
        BOARD_R_TOP_OHMS, BOARD_R_SERIES_OHMS,
        BOARD_ADS_VFS, BOARD_ADC_VREF, BOARD_ADC_MAX
    );

    if (r.resistance_ohms >= OPEN_CIRCUIT_RES_OHMS) {
        uint16_t count = bump_warn_count(mgr, idx, SENSOR_WARN_OPEN_OHMS);
        if (should_emit_warn(count)) {
            ESP_LOGW(TAG, "ch%d: likely open circuit (%.0f ohms)%s",
                     idx, r.resistance_ohms, count > 1 ? " (repeating)" : "");
        }
        return r;
    }

    float raw_temp = therm_math_resistance_to_celsius(r.resistance_ohms, &ch_cfg->sh);

    // NaN propagation check (e.g. negative resistance fed to log)
    if (isnanf(raw_temp) || isinff(raw_temp)) {
        uint16_t count = bump_warn_count(mgr, idx, SENSOR_WARN_TEMP_NAN);
        if (should_emit_warn(count)) {
            ESP_LOGW(TAG, "ch%d: temperature computation produced NaN/Inf%s", idx,
                     count > 1 ? " (repeating)" : "");
        }
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

    ESP_LOGI(TAG, "Sensor task running");

    // Register this task with the task watchdog.
    esp_task_wdt_add(NULL);

    while (1) {
        device_config_t cfg;
        config_mgr_get_active(mgr->config, &cfg);

        sensor_snapshot_t snap = {
            .timestamp_ms = esp_timer_get_time() / 1000LL,
        };

        for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
            // Skip channel for this tick if it has exceeded the error threshold.
            if (mgr->consecutive_errors[i] > CONSEC_ERROR_SKIP_THRESHOLD) {
                uint16_t count = bump_warn_count(mgr, i, SENSOR_WARN_SKIP);
                if (should_emit_warn(count)) {
                    ESP_LOGW(TAG, "ch%d: skipping (consecutive_errors=%u)%s", i,
                             mgr->consecutive_errors[i], count > 1 ? " (repeating)" : "");
                }
                // Preserve the last snapshot reading so callers still see stale data.
                xSemaphoreTake(mgr->lock, portMAX_DELAY);
                snap.channels[i] = mgr->snapshot.channels[i];
                xSemaphoreGive(mgr->lock);
                // Decay the counter so the channel is retried after a while.
                mgr->consecutive_errors[i]--;
                continue;
            }

            snap.channels[i] = sample_channel(mgr, i, &cfg.channels[i],
                                              cfg.ema_alpha, cfg.spike_reject_delta_c);

            if (snap.channels[i].quality == SENSOR_QUALITY_ERROR) {
                if (mgr->consecutive_errors[i] < UINT8_MAX) {
                    mgr->consecutive_errors[i]++;
                }
            } else {
                mgr->consecutive_errors[i] = 0;
                memset(mgr->warn_repeat_counts[i], 0, sizeof(mgr->warn_repeat_counts[i]));
            }
        }

        // Count cycles where every enabled channel failed — trigger I2C bus
        // reset before the task watchdog fires (which would reset the chip).
        bool any_ok = false;
        for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
            if (snap.channels[i].quality == SENSOR_QUALITY_OK) {
                any_ok = true;
                break;
            }
        }
        if (!any_ok) {
            if (mgr->all_error_cycles < UINT8_MAX) mgr->all_error_cycles++;
            if (mgr->all_error_cycles >= I2C_BUS_RESET_ERROR_CYCLES) {
                i2c_manager_reset_bus();
                mgr->all_error_cycles = 0;
            }
        } else {
            mgr->all_error_cycles = 0;
        }

        xSemaphoreTake(mgr->lock, portMAX_DELAY);
        mgr->snapshot       = snap;
        mgr->snapshot_valid = true;
        xSemaphoreGive(mgr->lock);

        // Signal SSE clients that a new snapshot is available.
        xEventGroupSetBits(mgr->snapshot_event_group, SENSOR_MGR_SNAPSHOT_BIT);

        // ── History downsampling ──────────────────────────────────────────────
        // Accumulate per-channel OK readings; flush one averaged point per minute.
        for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
            if (snap.channels[i].quality == SENSOR_QUALITY_OK) {
                mgr->hist_acc[i]   += snap.channels[i].temperature_c;
                mgr->hist_acc_n[i] += 1;
            }
        }
        if (snap.timestamp_ms >= mgr->history_next_ms) {
            history_point_t pt;
            pt.timestamp_s = (int32_t)(snap.timestamp_ms / 1000);
            for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
                if (mgr->hist_acc_n[i] > 0) {
                    float avg = mgr->hist_acc[i] / (float)mgr->hist_acc_n[i];
                    pt.temp_x10[i] = (int16_t)(avg * 10.0f);
                } else {
                    pt.temp_x10[i] = INT16_MIN;  // sentinel: no data
                }
                mgr->hist_acc[i]   = 0.0f;
                mgr->hist_acc_n[i] = 0;
            }
            xSemaphoreTake(mgr->lock, portMAX_DELAY);
            mgr->history[mgr->history_head] = pt;
            mgr->history_head = (mgr->history_head + 1) % HISTORY_POINTS;
            if (mgr->history_count < HISTORY_POINTS) mgr->history_count++;
            xSemaphoreGive(mgr->lock);

            mgr->history_next_ms = snap.timestamp_ms + HISTORY_INTERVAL_MS;
        }

        // Pat the watchdog after a successful sampling cycle.
        esp_task_wdt_reset();

        tick++;
        if (tick % LOG_EVERY_N_TICKS == 0) {
            char buf[128];
            int pos = snprintf(buf, sizeof(buf), "tick=%lu", (unsigned long)tick);
            for (int i = 0; i < CONFIG_NUM_CHANNELS && pos < (int)sizeof(buf) - 1; i++) {
                float f = snap.channels[i].temperature_c * 9.0f / 5.0f + 32.0f;
                pos += snprintf(buf + pos, sizeof(buf) - pos, "  ch%d=%.1f°C/%.1f°F (%s)",
                                i, snap.channels[i].temperature_c, f,
                                snap.channels[i].quality == SENSOR_QUALITY_OK ? "ok" : "err");
            }
            ESP_LOGI(TAG, "%s", buf);
        }

        // Clamp sample rate and compute delay.
        float rate = cfg.sample_rate_hz;
        if (rate < SAMPLE_RATE_MIN_HZ) rate = SAMPLE_RATE_MIN_HZ;
        if (rate > SAMPLE_RATE_MAX_HZ) rate = SAMPLE_RATE_MAX_HZ;
        uint32_t delay_ms = (uint32_t)(1000.0f / rate);

        // Account for ADS1115 conversion time: each channel performs one
        // settle-discard conversion plus oversampled conversions.
        uint32_t reads_per_channel = ADS_SETTLE_DISCARD_SAMPLES + ADS_OVERSAMPLE_SAMPLES;
        uint32_t read_time_ms = CONFIG_NUM_CHANNELS * reads_per_channel * 12;
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
    mgr->snapshot_event_group = xEventGroupCreate();
    if (!mgr->snapshot_event_group) {
        vSemaphoreDelete(mgr->lock);
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
        sensor_task, "sensor_mgr", 6144, mgr,
        5,   // priority: same as HTTP server default
        &mgr->task_handle
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

int sensor_mgr_copy_history(sensor_mgr_t *mgr,
                             sensor_history_point_t *buf, int max_points)
{
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    int count = mgr->history_count;
    if (count > max_points) count = max_points;
    // Copy oldest-first: oldest entry is at (head - history_count) mod HISTORY_POINTS.
    int oldest = (mgr->history_head - mgr->history_count + HISTORY_POINTS) % HISTORY_POINTS;
    for (int i = 0; i < count; i++) {
        int src = (oldest + i) % HISTORY_POINTS;
        buf[i].timestamp_s = mgr->history[src].timestamp_s;
        for (int ch = 0; ch < CONFIG_NUM_CHANNELS; ch++) {
            buf[i].temp_x10[ch] = mgr->history[src].temp_x10[ch];
        }
    }
    xSemaphoreGive(mgr->lock);
    return count;
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

EventGroupHandle_t sensor_mgr_get_event_group(sensor_mgr_t *mgr)
{
    return mgr->snapshot_event_group;
}

void sensor_mgr_set_stall_detected(sensor_mgr_t *mgr, int channel, bool stall_detected)
{
    if (!mgr || channel < 0 || channel >= CONFIG_NUM_CHANNELS) return;
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    mgr->snapshot.channels[channel].stall_detected = stall_detected;
    xSemaphoreGive(mgr->lock);
}

void sensor_mgr_deinit(sensor_mgr_t *mgr)
{
    if (!mgr) return;
    if (mgr->task_handle)          vTaskDelete(mgr->task_handle);
    if (mgr->lock)                 vSemaphoreDelete(mgr->lock);
    if (mgr->snapshot_event_group) vEventGroupDelete(mgr->snapshot_event_group);
    free(mgr);
}
