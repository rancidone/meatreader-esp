// Route handler: GET /events
// Server-Sent Events stream — pushes a snapshot JSON payload after each
// sensor update, using a FreeRTOS event group bit for wakeup.

#include "http_util.h"
#include "sensor_mgr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "route_events";

// Timeout between event group waits.  If the sensor task stalls we still
// want the handler to exit so the client can reconnect instead of holding
// the connection open indefinitely.
#define SSE_WAIT_TIMEOUT_MS  2000

// GET /events
// Streams sensor snapshots as Server-Sent Events.
// Each event has the form:
//   data: {json}\n\n
// A retry hint is sent once at the start of the stream.
esp_err_t handle_events(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;
    if (!ctx || !ctx->sensor || !ctx->config) {
        ESP_LOGW(TAG, "GET /events → unavailable (context not ready)");
        return send_error(req, 503, "events unavailable");
    }

    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // Tell the client to reconnect after 3 s if the connection drops.
    esp_err_t err = httpd_resp_send_chunk(req, "retry: 3000\n\n", 13);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "GET /events → client gone before first chunk");
        return ESP_OK;
    }

    EventGroupHandle_t eg = sensor_mgr_get_event_group(ctx->sensor);

    ESP_LOGI(TAG, "GET /events → SSE stream open");

    while (1) {
        // Wait for a new snapshot, with timeout so we can detect disconnects.
        EventBits_t bits = xEventGroupWaitBits(
            eg,
            SENSOR_MGR_SNAPSHOT_BIT,
            pdTRUE,   // clear on exit
            pdFALSE,  // wait for any (only one bit)
            pdMS_TO_TICKS(SSE_WAIT_TIMEOUT_MS)
        );

        if (!(bits & SENSOR_MGR_SNAPSHOT_BIT)) {
            // Timeout — send a comment as a keep-alive ping and check if
            // the client is still connected.
            err = httpd_resp_send_chunk(req, ": keep-alive\n\n", 14);
            if (err != ESP_OK) {
                ESP_LOGD(TAG, "GET /events → client disconnected (keep-alive failed)");
                return ESP_OK;
            }
            continue;
        }

        sensor_snapshot_t snap;
        if (!sensor_mgr_get_latest(ctx->sensor, &snap)) {
            // Bit was set but snapshot not valid yet — race at startup, skip.
            continue;
        }

        device_config_t active;
        config_mgr_get_active(ctx->config, &active);

        cJSON *obj = snapshot_to_json(&snap, active.alerts);
        char  *json_str = cJSON_PrintUnformatted(obj);
        cJSON_Delete(obj);

        if (!json_str) {
            ESP_LOGW(TAG, "GET /events → cJSON serialization failed");
            continue;
        }

        // Build "data: {json}\n\n" in two chunks to avoid a malloc for the
        // prefix+suffix, keeping heap pressure low on the ESP32.
        err = httpd_resp_send_chunk(req, "data: ", 6);
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(req, json_str, strlen(json_str));
        }
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(req, "\n\n", 2);
        }
        free(json_str);

        if (err != ESP_OK) {
            ESP_LOGD(TAG, "GET /events → client disconnected");
            return ESP_OK;
        }
    }
}
