// Route handlers: GET /temps/latest, GET /temps/history

#include "http_util.h"
#include "esp_log.h"
#include <stdlib.h>
#include <stdint.h>

static const char *TAG = "route_temps";

// GET /temps/latest
// Returns the latest sensor snapshot, or 503 if no data yet.
// Includes "alert_triggered" per channel if alert config is available.
esp_err_t handle_temps_latest(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    sensor_snapshot_t snap;
    if (!sensor_mgr_get_latest(ctx->sensor, &snap)) {
        ESP_LOGD(TAG, "GET /temps/latest → 503 no data yet");
        return send_error(req, 503, "no data yet — sensor task not ready");
    }

    device_config_t active;
    config_mgr_get_active(ctx->config, &active);

    ESP_LOGD(TAG, "GET /temps/latest → 200");
    return send_json(req, snapshot_to_json(&snap, active.alerts), 200);
}

// GET /temps/history
// Returns the firmware-side downsampled history ring buffer.
// One point per minute, up to 360 points (6 hours).
// Response: [{"t": <unix_s>, "ch": [<temp_c_or_null>, ...]}, ...]
esp_err_t handle_temps_history(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    sensor_history_point_t *buf = malloc(SENSOR_HISTORY_POINTS * sizeof(sensor_history_point_t));
    if (!buf) return send_error(req, 503, "OOM");

    int count = sensor_mgr_copy_history(ctx->sensor, buf, SENSOR_HISTORY_POINTS);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *pt = cJSON_CreateObject();
        cJSON_AddNumberToObject(pt, "t", buf[i].timestamp_s);
        cJSON *ch_arr = cJSON_CreateArray();
        for (int ch = 0; ch < CONFIG_NUM_CHANNELS; ch++) {
            if (buf[i].temp_x10[ch] == INT16_MIN) {
                cJSON_AddItemToArray(ch_arr, cJSON_CreateNull());
            } else {
                cJSON_AddItemToArray(ch_arr,
                    cJSON_CreateNumber((double)buf[i].temp_x10[ch] / 10.0));
            }
        }
        cJSON_AddItemToObject(pt, "ch", ch_arr);
        cJSON_AddItemToArray(arr, pt);
    }
    free(buf);

    ESP_LOGD(TAG, "GET /temps/history → 200 (%d points)", count);
    return send_json(req, arr, 200);
}
