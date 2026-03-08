// alert_mgr.c — per-channel temperature alert management.
//
// Internal task polls sensor_mgr_get_latest() each second (tracking last
// timestamp to detect new snapshots). For each channel with an enabled alert:
//   - Fire when temp >= target && !triggered  → set triggered = true in active config
//   - Clear when triggered && temp < target - 5°C  → set triggered = false
// Dispatch: webhook (esp_http_client) or MQTT (log warning, not yet implemented).
//
// Stall detection: per-channel rolling window of the last STALL_WINDOW_SAMPLES
// readings. A stall is flagged when the absolute temperature delta over the
// most-recent 20-minute span of samples is < 2°C. The stall clears when the
// temperature rises > 1°C over any 5-minute window.

#include "alert_mgr.h"
#include "config_mgr.h"
#include "sensor_mgr.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "alert_mgr";

// ── Webhook dispatch ──────────────────────────────────────────────────────────

typedef struct {
    char   url[128];
    int    channel;
    float  temp;
    float  target;
} webhook_args_t;

static void webhook_task(void *arg)
{
    webhook_args_t *args = (webhook_args_t *)arg;

    // Build JSON body: {"channel":N,"temp":T,"target":U}
    cJSON *body = cJSON_CreateObject();
    cJSON_AddNumberToObject(body, "channel", args->channel);
    cJSON_AddNumberToObject(body, "temp",    (double)args->temp);
    cJSON_AddNumberToObject(body, "target",  (double)args->target);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    if (!body_str) {
        ESP_LOGE(TAG, "ch%d: failed to serialize webhook body", args->channel);
        free(args);
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t cfg = {
        .url = args->url,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "ch%d: esp_http_client_init failed", args->channel);
        free(body_str);
        free(args);
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body_str, (int)strlen(body_str));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "ch%d: webhook POST %s → HTTP %d", args->channel, args->url, status);
    } else {
        ESP_LOGE(TAG, "ch%d: webhook POST failed: %s", args->channel, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(body_str);
    free(args);
    vTaskDelete(NULL);
}

static void dispatch_webhook(int channel, float temp, float target, const char *url)
{
    webhook_args_t *args = malloc(sizeof(webhook_args_t));
    if (!args) {
        ESP_LOGE(TAG, "ch%d: OOM allocating webhook args", channel);
        return;
    }
    args->channel = channel;
    args->temp    = temp;
    args->target  = target;
    strncpy(args->url, url, sizeof(args->url) - 1);
    args->url[sizeof(args->url) - 1] = '\0';

    if (xTaskCreate(webhook_task, "alert_webhook", 4096, args, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "ch%d: failed to create webhook task", channel);
        free(args);
    }
}

// ── Stall detection ───────────────────────────────────────────────────────────

// Rolling window size — at 1 sample/s, 1200 samples covers 20 minutes.
#define STALL_WINDOW_SAMPLES  1200

// Millisecond thresholds for stall detection.
#define STALL_DETECT_WINDOW_MS   (20 * 60 * 1000LL)   // 20 minutes
#define STALL_DETECT_DELTA_C     2.0f                  // must rise > this to avoid stall
#define STALL_CLEAR_WINDOW_MS    (5 * 60 * 1000LL)    // 5 minutes
#define STALL_CLEAR_DELTA_C      1.0f                  // must rise > this to clear stall

typedef struct {
    float   temp_c;
    int64_t timestamp_ms;   // ms since boot (esp_timer_get_time() / 1000)
} stall_sample_t;

typedef struct {
    stall_sample_t buf[STALL_WINDOW_SAMPLES];
    int            head;          // ring-buffer write index (next slot to write)
    int            count;         // number of valid samples (up to STALL_WINDOW_SAMPLES)
    bool           stall_detected;
    bool           stall_alert_fired; // prevents re-firing until stall clears
} channel_stall_state_t;

// ── Alert manager struct ──────────────────────────────────────────────────────

struct alert_mgr {
    config_mgr_t        *config;
    sensor_mgr_t        *sensor;
    TaskHandle_t         task_handle;
    volatile bool        running;
    channel_stall_state_t stall[CONFIG_NUM_CHANNELS];
};

// ── Stall detection helpers ───────────────────────────────────────────────────

// Push one temperature reading into the per-channel rolling window.
static void stall_push(channel_stall_state_t *st, float temp_c, int64_t now_ms)
{
    st->buf[st->head].temp_c       = temp_c;
    st->buf[st->head].timestamp_ms = now_ms;
    st->head = (st->head + 1) % STALL_WINDOW_SAMPLES;
    if (st->count < STALL_WINDOW_SAMPLES) {
        st->count++;
    }
}

// Return the index (0..count-1) of the sample that is >= window_ms before
// now_ms, searching backwards from the newest sample. Returns -1 if no
// sample is old enough.
static int stall_find_oldest_in_window(const channel_stall_state_t *st,
                                        int64_t now_ms, int64_t window_ms)
{
    if (st->count == 0) return -1;

    // newest sample is at index (head - 1 + STALL_WINDOW_SAMPLES) % STALL_WINDOW_SAMPLES
    // oldest sample is at index (head - count + STALL_WINDOW_SAMPLES) % STALL_WINDOW_SAMPLES
    int oldest_buf_idx = (st->head - st->count + STALL_WINDOW_SAMPLES) % STALL_WINDOW_SAMPLES;

    // Walk from oldest to newest; return the oldest whose age >= window_ms.
    for (int i = 0; i < st->count; i++) {
        int idx = (oldest_buf_idx + i) % STALL_WINDOW_SAMPLES;
        int64_t age_ms = now_ms - st->buf[idx].timestamp_ms;
        if (age_ms >= window_ms) {
            return idx;
        }
    }
    return -1;
}

// Return the newest sample's temperature. Caller must ensure count > 0.
static float stall_newest_temp(const channel_stall_state_t *st)
{
    int newest_idx = (st->head - 1 + STALL_WINDOW_SAMPLES) % STALL_WINDOW_SAMPLES;
    return st->buf[newest_idx].temp_c;
}

// Update stall state for one channel. Returns true if stall_detected changed
// and a webhook alert should be dispatched.
static bool stall_update(channel_stall_state_t *st, const channel_alert_t *alert,
                          float temp_c, int64_t now_ms, int channel)
{
    stall_push(st, temp_c, now_ms);

    bool should_fire = false;

    // ── Check for stall (20-minute window, delta < 2°C) ──
    int anchor_idx = stall_find_oldest_in_window(st, now_ms, STALL_DETECT_WINDOW_MS);
    if (anchor_idx >= 0) {
        float anchor_temp = st->buf[anchor_idx].temp_c;
        float newest      = stall_newest_temp(st);
        float delta       = fabsf(newest - anchor_temp);

        if (delta < STALL_DETECT_DELTA_C) {
            if (!st->stall_detected) {
                st->stall_detected = true;
                ESP_LOGI("alert_mgr",
                         "ch%d: STALL detected (delta=%.2f°C over 20 min)",
                         channel, delta);
            }
            // Fire stall alert once per stall event if configured.
            if (st->stall_detected && !st->stall_alert_fired &&
                alert->stall_alert_enabled) {
                should_fire = true;
                st->stall_alert_fired = true;
            }
        }
    }

    // ── Check for stall clear (5-minute window, delta > 1°C rising) ──
    if (st->stall_detected) {
        int clear_idx = stall_find_oldest_in_window(st, now_ms, STALL_CLEAR_WINDOW_MS);
        if (clear_idx >= 0) {
            float base    = st->buf[clear_idx].temp_c;
            float newest  = stall_newest_temp(st);
            float rise    = newest - base;   // directional: positive = rising
            if (rise > STALL_CLEAR_DELTA_C) {
                st->stall_detected    = false;
                st->stall_alert_fired = false;
                ESP_LOGI("alert_mgr",
                         "ch%d: stall cleared (rose %.2f°C over 5 min)",
                         channel, rise);
            }
        }
    }

    return should_fire;
}

// ── Core check logic ──────────────────────────────────────────────────────────

void alert_mgr_check_snapshot(alert_mgr_t *mgr, const sensor_snapshot_t *snap)
{
    device_config_t cfg;
    config_mgr_get_active(mgr->config, &cfg);

    bool config_changed = false;
    int64_t now_ms = (int64_t)(esp_timer_get_time() / 1000);

    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        channel_alert_t *alert = &cfg.alerts[i];
        const channel_reading_t *ch = &snap->channels[i];

        if (!alert->enabled) continue;
        if (ch->quality != SENSOR_QUALITY_OK) continue;

        float temp   = ch->temperature_c;
        float target = alert->target_temp_c;

        // ── Target-temperature alert ──────────────────────────────────────────
        if (!alert->triggered && temp >= target) {
            // Fire alert.
            ESP_LOGI(TAG, "ch%d: ALERT fired! temp=%.2f°C >= target=%.2f°C",
                     i, temp, target);
            alert->triggered = true;
            config_changed   = true;

            switch (alert->method) {
                case ALERT_METHOD_WEBHOOK:
                    if (alert->webhook_url[0] != '\0') {
                        dispatch_webhook(i, temp, target, alert->webhook_url);
                    } else {
                        ESP_LOGW(TAG, "ch%d: ALERT_METHOD_WEBHOOK but webhook_url is empty", i);
                    }
                    break;
                case ALERT_METHOD_MQTT:
                    ESP_LOGW(TAG, "ch%d: MQTT not yet implemented", i);
                    break;
                case ALERT_METHOD_NONE:
                default:
                    break;
            }

        } else if (alert->triggered && temp < (target - 5.0f)) {
            // Hysteresis: clear triggered once temp drops 5°C below target.
            ESP_LOGI(TAG, "ch%d: alert cleared (temp=%.2f°C < target-5=%.2f°C)",
                     i, temp, target - 5.0f);
            alert->triggered = false;
            config_changed   = true;
        }

        // ── Stall detection ───────────────────────────────────────────────────
        bool fire_stall = stall_update(&mgr->stall[i], alert, temp, now_ms, i);
        // Write current stall state back into the sensor snapshot so SSE sees it.
        sensor_mgr_set_stall_detected(mgr->sensor, i, mgr->stall[i].stall_detected);
        if (fire_stall) {
            ESP_LOGI(TAG, "ch%d: STALL ALERT fired! (temp stagnant over 20 min)", i);
            switch (alert->method) {
                case ALERT_METHOD_WEBHOOK:
                    if (alert->webhook_url[0] != '\0') {
                        // Re-use dispatch_webhook; target field carries current
                        // temp (no meaningful "target" for stall alerts).
                        dispatch_webhook(i, temp, temp, alert->webhook_url);
                    } else {
                        ESP_LOGW(TAG, "ch%d: stall ALERT_METHOD_WEBHOOK but webhook_url is empty", i);
                    }
                    break;
                case ALERT_METHOD_MQTT:
                    ESP_LOGW(TAG, "ch%d: stall MQTT not yet implemented", i);
                    break;
                case ALERT_METHOD_NONE:
                default:
                    break;
            }
        }
    }

    if (config_changed) {
        // Write updated triggered state back into active config.
        // We do a staged patch → apply cycle to keep the config_mgr interface clean.
        // Simpler: directly update staged + apply so triggered state is live.
        config_mgr_set_staged(mgr->config, &cfg);
        config_mgr_apply(mgr->config);
    }
}

// ── Public stall state accessor ───────────────────────────────────────────────

bool alert_mgr_get_stall_detected(alert_mgr_t *mgr, int channel)
{
    if (!mgr || channel < 0 || channel >= CONFIG_NUM_CHANNELS) return false;
    return mgr->stall[channel].stall_detected;
}

// ── Background task ───────────────────────────────────────────────────────────

static void alert_task(void *arg)
{
    alert_mgr_t *mgr = (alert_mgr_t *)arg;
    int64_t last_timestamp = -1;

    while (mgr->running) {
        sensor_snapshot_t snap;
        if (sensor_mgr_get_latest(mgr->sensor, &snap)) {
            // Only process if this is a new snapshot.
            if (snap.timestamp_ms != last_timestamp) {
                last_timestamp = snap.timestamp_ms;
                alert_mgr_check_snapshot(mgr, &snap);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

esp_err_t alert_mgr_init(config_mgr_t *config, sensor_mgr_t *sensor,
                          alert_mgr_t **out)
{
    alert_mgr_t *mgr = calloc(1, sizeof(alert_mgr_t));
    if (!mgr) return ESP_ERR_NO_MEM;

    mgr->config  = config;
    mgr->sensor  = sensor;
    mgr->running = true;

    if (xTaskCreate(alert_task, "alert_mgr", 4096, mgr, 3, &mgr->task_handle) != pdPASS) {
        free(mgr);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "alert_mgr started");
    *out = mgr;
    return ESP_OK;
}

void alert_mgr_deinit(alert_mgr_t *mgr)
{
    if (!mgr) return;
    mgr->running = false;
    // Give the task time to exit.
    vTaskDelay(pdMS_TO_TICKS(1500));
    if (mgr->task_handle) {
        vTaskDelete(mgr->task_handle);
    }
    free(mgr);
}
