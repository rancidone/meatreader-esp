// Route handlers: GET /temps/latest

#include "http_util.h"
#include "esp_log.h"

static const char *TAG = "route_temps";

// GET /temps/latest
// Returns the latest sensor snapshot, or 503 if no data yet.
esp_err_t handle_temps_latest(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    sensor_snapshot_t snap;
    if (!sensor_mgr_get_latest(ctx->sensor, &snap)) {
        ESP_LOGD(TAG, "GET /temps/latest → 503 no data yet");
        return send_error(req, 503, "no data yet — sensor task not ready");
    }

    ESP_LOGD(TAG, "GET /temps/latest → 200");
    return send_json(req, snapshot_to_json(&snap), 200);
}
