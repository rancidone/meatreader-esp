// HTTP server lifecycle and URI handler registration.

#include "http_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "http_server";

static httpd_handle_t    s_server  = NULL;
static http_app_ctx_t    s_ctx;

// Forward declarations — normal operation routes
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
esp_err_t handle_events(httpd_req_t *req);

// Forward declarations — provisioning routes (routes_provision.c)
esp_err_t handle_provision_root(httpd_req_t *req);
esp_err_t handle_captive_redirect(httpd_req_t *req);
esp_err_t handle_provision_connect(httpd_req_t *req);
esp_err_t handle_provision_status(httpd_req_t *req);

// ── Normal URI table ──────────────────────────────────────────────────────────

static const httpd_uri_t s_normal_uris[] = {
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
    { .uri = "/calibration/live",          .method = HTTP_GET,  .handler = handle_cal_live          },
    { .uri = "/calibration/session/start", .method = HTTP_POST, .handler = handle_cal_session_start },
    { .uri = "/calibration/point/capture", .method = HTTP_POST, .handler = handle_cal_point_capture },
    { .uri = "/calibration/solve",         .method = HTTP_POST, .handler = handle_cal_solve         },
    { .uri = "/calibration/accept",        .method = HTTP_POST, .handler = handle_cal_accept        },
    // Prometheus metrics
    { .uri = "/metrics",                   .method = HTTP_GET,  .handler = handle_metrics           },
    // Server-Sent Events stream
    { .uri = "/events",                    .method = HTTP_GET,  .handler = handle_events            },
};

#define NUM_NORMAL_URIS  (sizeof(s_normal_uris) / sizeof(s_normal_uris[0]))

// ── Provisioning URI table ────────────────────────────────────────────────────

static const httpd_uri_t s_provision_uris[] = {
    { .uri = "/",                    .method = HTTP_GET,  .handler = handle_provision_root    },
    { .uri = "/provision/connect",   .method = HTTP_POST, .handler = handle_provision_connect },
    { .uri = "/provision/status",    .method = HTTP_GET,  .handler = handle_provision_status  },
    // Captive portal detection probes — OS-specific
    { .uri = "/generate_204",        .method = HTTP_GET,  .handler = handle_captive_redirect  },
    { .uri = "/hotspot-detect.html", .method = HTTP_GET,  .handler = handle_captive_redirect  },
    { .uri = "/ncsi.txt",            .method = HTTP_GET,  .handler = handle_captive_redirect  },
    { .uri = "/connecttest.txt",     .method = HTTP_GET,  .handler = handle_captive_redirect  },
    { .uri = "/redirect",            .method = HTTP_GET,  .handler = handle_captive_redirect  },
};

#define NUM_PROVISION_URIS  (sizeof(s_provision_uris) / sizeof(s_provision_uris[0]))

// ── Lifecycle ─────────────────────────────────────────────────────────────────

esp_err_t http_server_start(const http_app_ctx_t *ctx)
{
    s_ctx = *ctx;

    int num_uris = ctx->provisioning
                   ? (int)NUM_PROVISION_URIS
                   : (int)NUM_NORMAL_URIS;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 80;
    config.max_uri_handlers = num_uris + 2;
    config.stack_size       = 8192;
    config.uri_match_fn     = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t *uris = ctx->provisioning ? s_provision_uris : s_normal_uris;
    for (int i = 0; i < num_uris; i++) {
        httpd_uri_t uri = uris[i];
        uri.user_ctx = &s_ctx;
        httpd_register_uri_handler(s_server, &uri);
    }

    ESP_LOGI(TAG, "HTTP server started (%d routes, %s mode)",
             num_uris, ctx->provisioning ? "provisioning" : "normal");
    return ESP_OK;
}

void http_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
