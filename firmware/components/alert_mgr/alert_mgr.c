// alert_mgr.c — per-channel temperature alert management.
//
// Internal task polls sensor_mgr_get_latest() each second (tracking last
// timestamp to detect new snapshots). For each channel with an enabled alert:
//   - Fire when temp >= target && !triggered  → set triggered = true in active config
//   - Clear when triggered && temp < target - 5°C  → set triggered = false
// Dispatch: webhook (esp_http_client) or MQTT (log warning, not yet implemented).

#include "alert_mgr.h"
#include "config_mgr.h"
#include "sensor_mgr.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

// ── Alert manager struct ──────────────────────────────────────────────────────

struct alert_mgr {
    config_mgr_t  *config;
    sensor_mgr_t  *sensor;
    TaskHandle_t   task_handle;
    volatile bool  running;
};

// ── Core check logic ──────────────────────────────────────────────────────────

void alert_mgr_check_snapshot(alert_mgr_t *mgr, const sensor_snapshot_t *snap)
{
    device_config_t cfg;
    config_mgr_get_active(mgr->config, &cfg);

    bool config_changed = false;

    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        channel_alert_t *alert = &cfg.alerts[i];
        const channel_reading_t *ch = &snap->channels[i];

        if (!alert->enabled) continue;
        if (ch->quality != SENSOR_QUALITY_OK) continue;

        float temp   = ch->temperature_c;
        float target = alert->target_temp_c;

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
    }

    if (config_changed) {
        // Write updated triggered state back into active config.
        // We do a staged patch → apply cycle to keep the config_mgr interface clean.
        // Simpler: directly update staged + apply so triggered state is live.
        config_mgr_set_staged(mgr->config, &cfg);
        config_mgr_apply(mgr->config);
    }
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
