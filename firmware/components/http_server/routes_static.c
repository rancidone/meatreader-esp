// Catch-all static file server — serves the Svelte UI from SPIFFS.

#include <string.h>
#include <stdlib.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include <sys/stat.h>

static const char *TAG = "routes_static";

#define SPIFFS_BASE  "/spiffs"
#define CHUNK_SIZE   1024

static const char *content_type_for(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0)        return "text/html";
    if (strcmp(ext, ".js") == 0)          return "application/javascript";
    if (strcmp(ext, ".css") == 0)         return "text/css";
    if (strcmp(ext, ".webmanifest") == 0) return "application/manifest+json";
    if (strcmp(ext, ".png") == 0)         return "image/png";
    if (strcmp(ext, ".svg") == 0)         return "image/svg+xml";
    if (strcmp(ext, ".ico") == 0)         return "image/x-icon";
    if (strcmp(ext, ".json") == 0)        return "application/json";
    return "application/octet-stream";
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static esp_err_t serve_file(httpd_req_t *req, const char *filepath)
{
    FILE *f = fopen(filepath, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type_for(filepath));

    // Cache headers: long-lived immutable for hashed assets, no-cache for index.html.
    if (strstr(filepath, "/assets/")) {
        httpd_resp_set_hdr(req, "Cache-Control", "max-age=31536000, immutable");
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    }

    char *chunk = malloc(CHUNK_SIZE);
    if (!chunk) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    size_t read_bytes;
    do {
        read_bytes = fread(chunk, 1, CHUNK_SIZE, f);
        if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                fclose(f);
                free(chunk);
                return ESP_FAIL;
            }
        }
    } while (read_bytes == CHUNK_SIZE);

    fclose(f);
    free(chunk);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t routes_static_handler(httpd_req_t *req)
{
    const char *uri = req->uri;
    char filepath[256];

    // Build full SPIFFS path.
    snprintf(filepath, sizeof(filepath), "%s%s", SPIFFS_BASE, uri);

    // Strip query string if present.
    char *q = strchr(filepath, '?');
    if (q) *q = '\0';

    // If path ends with '/', append index.html.
    size_t len = strlen(filepath);
    if (filepath[len - 1] == '/') {
        strncat(filepath, "index.html", sizeof(filepath) - len - 1);
    }

    // Try exact file.
    if (file_exists(filepath)) {
        ESP_LOGD(TAG, "serve %s", filepath);
        return serve_file(req, filepath);
    }

    // SPA fallback: serve index.html for any unmatched path.
    snprintf(filepath, sizeof(filepath), "%s/index.html", SPIFFS_BASE);
    if (file_exists(filepath)) {
        httpd_resp_set_status(req, "200 OK");
        ESP_LOGD(TAG, "SPA fallback for %s → index.html", uri);
        return serve_file(req, filepath);
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_FAIL;
}
