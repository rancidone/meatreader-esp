// Route handlers: GET /status, GET /device

#include "http_util.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"

static const char *TAG = "route_status";

// Staleness threshold: if the last snapshot is older than this, flag it.
#define STALE_THRESHOLD_MS  5000

// GET /status
// Returns a health summary: uptime, firmware version, health flags.
esp_err_t handle_status(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    int64_t now_ms = esp_timer_get_time() / 1000LL;

    // Build health flags.
    cJSON *flags = cJSON_CreateArray();
    bool healthy = true;

    sensor_snapshot_t snap;
    bool has_data = sensor_mgr_get_latest(ctx->sensor, &snap);

    if (!has_data) {
        cJSON_AddItemToArray(flags, cJSON_CreateString("no_data"));
        healthy = false;
    } else {
        int64_t age_ms = now_ms - snap.timestamp_ms;
        if (age_ms > STALE_THRESHOLD_MS) {
            cJSON_AddItemToArray(flags, cJSON_CreateString("stale_data"));
            healthy = false;
        }
        for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
            if (snap.channels[i].quality != SENSOR_QUALITY_OK) {
                char flag[32];
                const char *q = snap.channels[i].quality == SENSOR_QUALITY_ERROR
                                    ? "error" : "disabled";
                snprintf(flag, sizeof(flag), "ch%d:%s", i, q);
                cJSON_AddItemToArray(flags, cJSON_CreateString(flag));
                healthy = false;
            }
        }
    }

    // Read WiFi RSSI; emit 0 if not connected or call fails.
    int rssi = 0;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    bool wifi_provisioned = ctx->config != NULL &&
                            !ctx->provisioning;

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "status",           healthy ? "ok" : "degraded");
    cJSON_AddBoolToObject(obj,   "healthy",          healthy);
    cJSON_AddItemToObject(obj,   "health_flags",     flags);
    cJSON_AddNumberToObject(obj, "uptime_ms",        (double)now_ms);
    cJSON_AddStringToObject(obj, "firmware",         FIRMWARE_VERSION);
    cJSON_AddNumberToObject(obj, "wifi_rssi_dbm",    rssi);
    cJSON_AddBoolToObject(obj,   "wifi_provisioned", wifi_provisioned);

    ESP_LOGD(TAG, "GET /status → 200 healthy=%s", healthy ? "true" : "false");
    return send_json(req, obj, 200);
}

// GET /device
// Returns static device metadata.
esp_err_t handle_device(httpd_req_t *req)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "platform",  "esp32");
    cJSON_AddStringToObject(obj, "firmware",  FIRMWARE_VERSION);
    cJSON_AddNumberToObject(obj, "channels",  CONFIG_NUM_CHANNELS);
    return send_json(req, obj, 200);
}
