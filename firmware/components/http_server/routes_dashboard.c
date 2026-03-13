// Route handler: GET /dashboard
//
// Consolidated read endpoint for mobile/native app clients.
// Returns snapshot + status + alerts in a single response, saving two
// extra round-trips compared to calling /temps/latest, /status, and
// /alerts separately.

#include "http_util.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_app_desc.h"
#include <string.h>

static const char *TAG = "route_dashboard";

#define STALE_THRESHOLD_MS 5000

// ── Alert JSON helpers (local; mirrors routes_alerts.c) ──────────────────────

static const char *dash_alert_method_str(alert_method_t method)
{
    switch (method) {
        case ALERT_METHOD_WEBHOOK: return "webhook";
        case ALERT_METHOD_MQTT:    return "mqtt";
        default:                   return "none";
    }
}

static cJSON *dash_alert_to_json(int ch, const channel_alert_t *a)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "channel",       ch);
    cJSON_AddBoolToObject(obj,   "enabled",       a->enabled);
    cJSON_AddNumberToObject(obj, "target_temp_c", (double)a->target_temp_c);
    cJSON_AddStringToObject(obj, "method",        dash_alert_method_str(a->method));
    cJSON_AddStringToObject(obj, "webhook_url",   a->webhook_url);
    cJSON_AddBoolToObject(obj,   "triggered",     a->triggered);
    return obj;
}

// ── Handler ──────────────────────────────────────────────────────────────────

// GET /dashboard
// Returns:
//   {
//     "snapshot": <same shape as GET /temps/latest>,
//     "status":   <same shape as GET /status>,
//     "alerts":   <same shape as GET /alerts>
//   }
// If sensor data is not yet available, "snapshot" is null and
// "status" reflects the no_data health flag. Never returns 503 —
// callers can always get a usable response for the connection/health
// indicator even before the first sensor reading.
esp_err_t handle_dashboard(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    // ── Snapshot ─────────────────────────────────────────────────────────────

    sensor_snapshot_t snap;
    bool has_data = sensor_mgr_get_latest(ctx->sensor, &snap);

    device_config_t active;
    config_mgr_get_active(ctx->config, &active);

    cJSON *snapshot_json = has_data
        ? snapshot_to_json(&snap, active.alerts)
        : cJSON_CreateNull();

    // ── Status ───────────────────────────────────────────────────────────────

    int64_t now_ms = esp_timer_get_time() / 1000LL;
    cJSON *flags   = cJSON_CreateArray();
    bool healthy   = true;

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

    int rssi = 0;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    bool wifi_provisioned = ctx->config != NULL && !ctx->provisioning;

    cJSON *status_json = cJSON_CreateObject();
    cJSON_AddStringToObject(status_json, "status",           healthy ? "ok" : "degraded");
    cJSON_AddBoolToObject(status_json,   "healthy",          healthy);
    cJSON_AddItemToObject(status_json,   "health_flags",     flags);
    cJSON_AddNumberToObject(status_json, "uptime_ms",        (double)now_ms);
    cJSON_AddStringToObject(status_json, "firmware",         FIRMWARE_VERSION);
    cJSON_AddNumberToObject(status_json, "wifi_rssi_dbm",    rssi);
    cJSON_AddBoolToObject(status_json,   "wifi_provisioned", wifi_provisioned);

    // ── Alerts ───────────────────────────────────────────────────────────────

    cJSON *alerts_json = cJSON_CreateArray();
    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        cJSON_AddItemToArray(alerts_json, dash_alert_to_json(i, &active.alerts[i]));
    }

    // ── Compose response ─────────────────────────────────────────────────────

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddItemToObject(resp, "snapshot", snapshot_json);
    cJSON_AddItemToObject(resp, "status",   status_json);
    cJSON_AddItemToObject(resp, "alerts",   alerts_json);

    ESP_LOGD(TAG, "GET /dashboard → 200 healthy=%s", healthy ? "true" : "false");
    return send_json(req, resp, 200);
}
