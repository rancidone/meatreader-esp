// Route handlers: GET /temps/latest

#include "http_util.h"
#include "esp_log.h"

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
