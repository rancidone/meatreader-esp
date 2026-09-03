// Serve the embedded Svelte UI from flash memory.
// Files are compiled into the firmware binary by gen_ui_assets.py;
// text assets are pre-gzipped and served with Content-Encoding: gzip.

#include "routes_static.h"
#include "ui_assets.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "routes_static";

esp_err_t routes_static_handler(httpd_req_t *req)
{
    const char *uri = req->uri;

    // Strip query string for lookup.
    char path[512];
    strncpy(path, uri, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    char *q = strchr(path, '?');
    if (q) *q = '\0';

    // Exact match in the routing table.
    for (size_t i = 0; i < ui_files_count; i++) {
        if (strcmp(path, ui_files[i].uri) == 0) {
            httpd_resp_set_type(req, ui_files[i].content_type);
            if (ui_files[i].gzipped) {
                httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
            }
            if (strncmp(path, "/assets/", 8) == 0) {
                httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");
            } else {
                httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
            }
            httpd_resp_send(req, (const char *)ui_files[i].data, (ssize_t)ui_files[i].size);
            return ESP_OK;
        }
    }

    // SPA fallback: unmatched paths get index.html for client-side routing.
    ESP_LOGD(TAG, "SPA fallback for %s → index.html", path);
    for (size_t i = 0; i < ui_files_count; i++) {
        if (strcmp(ui_files[i].uri, "/index.html") == 0) {
            httpd_resp_set_type(req, "text/html; charset=utf-8");
            httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
            httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
            httpd_resp_send(req, (const char *)ui_files[i].data, (ssize_t)ui_files[i].size);
            return ESP_OK;
        }
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_FAIL;
}
