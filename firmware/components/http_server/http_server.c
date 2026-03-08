// HTTP server lifecycle and URI handler registration.

#include "http_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "http_server";

static httpd_handle_t    s_server  = NULL;
static http_app_ctx_t    s_ctx;

// Forward declarations for handlers defined in routes_*.c
esp_err_t handle_temps_latest(httpd_req_t *req);
esp_err_t handle_status(httpd_req_t *req);
esp_err_t handle_device(httpd_req_t *req);
esp_err_t handle_config_get(httpd_req_t *req);
esp_err_t handle_config_patch_staged(httpd_req_t *req);
esp_err_t handle_config_apply(httpd_req_t *req);
esp_err_t handle_config_commit(httpd_req_t *req);
esp_err_t handle_config_revert_staged(httpd_req_t *req);
esp_err_t handle_config_revert_active(httpd_req_t *req);
esp_err_t handle_cal_live(httpd_req_t *req);
esp_err_t handle_cal_session_start(httpd_req_t *req);
esp_err_t handle_cal_point_capture(httpd_req_t *req);
esp_err_t handle_cal_solve(httpd_req_t *req);
esp_err_t handle_cal_accept(httpd_req_t *req);
esp_err_t handle_metrics(httpd_req_t *req);

// ── URI table ─────────────────────────────────────────────────────────────────

static const httpd_uri_t s_uris[] = {
    // Temperature
    { .uri = "/temps/latest",          .method = HTTP_GET,   .handler = handle_temps_latest       },
    // Status / device info
    { .uri = "/status",                .method = HTTP_GET,   .handler = handle_status             },
    { .uri = "/device",                .method = HTTP_GET,   .handler = handle_device             },
    // Config
    { .uri = "/config",                .method = HTTP_GET,   .handler = handle_config_get         },
    { .uri = "/config/staged",         .method = HTTP_PATCH, .handler = handle_config_patch_staged },
    { .uri = "/config/apply",          .method = HTTP_POST,  .handler = handle_config_apply       },
    { .uri = "/config/commit",         .method = HTTP_POST,  .handler = handle_config_commit      },
    { .uri = "/config/revert-staged",  .method = HTTP_POST,  .handler = handle_config_revert_staged },
    { .uri = "/config/revert-active",  .method = HTTP_POST,  .handler = handle_config_revert_active },
    // Calibration
    { .uri = "/calibration/live",         .method = HTTP_GET,  .handler = handle_cal_live          },
    { .uri = "/calibration/session/start",.method = HTTP_POST, .handler = handle_cal_session_start },
    { .uri = "/calibration/point/capture",.method = HTTP_POST, .handler = handle_cal_point_capture },
    { .uri = "/calibration/solve",        .method = HTTP_POST, .handler = handle_cal_solve         },
    { .uri = "/calibration/accept",       .method = HTTP_POST, .handler = handle_cal_accept        },
    // Prometheus metrics
    { .uri = "/metrics",                  .method = HTTP_GET,  .handler = handle_metrics           },
};

#define NUM_URIS  (sizeof(s_uris) / sizeof(s_uris[0]))

esp_err_t http_server_start(const http_app_ctx_t *ctx)
{
    s_ctx = *ctx;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 80;
    config.max_uri_handlers = NUM_URIS + 2;
    config.stack_size       = 8192;
    // Increase URI match strictness (no wildcard needed here)
    config.uri_match_fn     = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    // Register each URI handler, attaching the shared app context.
    for (int i = 0; i < (int)NUM_URIS; i++) {
        httpd_uri_t uri = s_uris[i];
        uri.user_ctx = &s_ctx;
        httpd_register_uri_handler(s_server, &uri);
    }

    ESP_LOGI(TAG, "HTTP server started (%d routes registered)", (int)NUM_URIS);
    return ESP_OK;
}

void http_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
