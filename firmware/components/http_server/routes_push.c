// routes_push.c — Web Push notification relay
//
// Implements two endpoints:
//
//   POST /push/subscription
//     Body: { "endpoint": "https://...", "p256dh": "<base64url>", "auth": "<base64url>" }
//     Stores the push subscription in NVS so the device can relay notifications
//     to the browser's push service even when no tab is open.
//
//   POST /push/notify
//     Body: { "title": "...", "body": "..." }   (optional — default text used if absent)
//     Makes an outbound HTTP POST to the stored push endpoint.
//
// VAPID signing notes:
//   Full VAPID (RFC 8292) requires ECDSA P-256 signing of a JWT and AES-GCM
//   encryption of the payload. mbedTLS (bundled in ESP-IDF) supports both, but
//   the implementation is non-trivial. For now this sends an unencrypted request
//   to the endpoint URL — sufficient for same-LAN push services or custom relay
//   servers that don't require VAPID. Production-quality VAPID signing is a
//   future enhancement.
//
// To use with real push services (FCM, Mozilla Push):
//   1. Implement VAPID JWT (ES256) + Web Push encryption (HKDF + AES-128-GCM)
//      using mbedtls_ecdsa_write_signature, mbedtls_hkdf, mbedtls_gcm_*.
//   2. Set Authorization: vapid t=<jwt>,k=<public-key> header on POST.

#include "http_util.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "route_push";

#define PUSH_NVS_NAMESPACE  "meatreader_push"
#define PUSH_NVS_KEY_EP     "endpoint_url"
#define PUSH_NVS_KEY_P256DH "p256dh"
#define PUSH_NVS_KEY_AUTH   "auth"

// Maximum endpoint URL length (push service URLs are typically 150-250 chars)
#define PUSH_ENDPOINT_MAX   512

// ── NVS helpers ───────────────────────────────────────────────────────────────

static esp_err_t push_nvs_set(const char *key, const char *value)
{
    nvs_handle_t hdl;
    esp_err_t err = nvs_open(PUSH_NVS_NAMESPACE, NVS_READWRITE, &hdl);
    if (err != ESP_OK) return err;
    err = nvs_set_str(hdl, key, value);
    if (err == ESP_OK) err = nvs_commit(hdl);
    nvs_close(hdl);
    return err;
}

static esp_err_t push_nvs_get(const char *key, char *out, size_t max_len)
{
    nvs_handle_t hdl;
    esp_err_t err = nvs_open(PUSH_NVS_NAMESPACE, NVS_READONLY, &hdl);
    if (err != ESP_OK) return err;
    size_t len = max_len;
    err = nvs_get_str(hdl, key, out, &len);
    nvs_close(hdl);
    return err;
}

// ── POST /push/subscription ───────────────────────────────────────────────────

esp_err_t handle_push_subscription(httpd_req_t *req)
{
    cJSON *body = read_json_body(req);
    if (!body) {
        return send_error(req, 400, "invalid or missing JSON body");
    }

    cJSON *ep_item = cJSON_GetObjectItemCaseSensitive(body, "endpoint");
    cJSON *p256_item = cJSON_GetObjectItemCaseSensitive(body, "p256dh");
    cJSON *auth_item = cJSON_GetObjectItemCaseSensitive(body, "auth");

    if (!cJSON_IsString(ep_item) || !ep_item->valuestring || ep_item->valuestring[0] == '\0') {
        cJSON_Delete(body);
        return send_error(req, 400, "endpoint is required");
    }

    if (strlen(ep_item->valuestring) >= PUSH_ENDPOINT_MAX) {
        cJSON_Delete(body);
        return send_error(req, 400, "endpoint URL too long");
    }

    esp_err_t err = push_nvs_set(PUSH_NVS_KEY_EP, ep_item->valuestring);
    if (err != ESP_OK) {
        cJSON_Delete(body);
        ESP_LOGE(TAG, "NVS write endpoint failed: %s", esp_err_to_name(err));
        return send_error(req, 500, "failed to store endpoint");
    }

    // Store optional p256dh and auth keys (used for payload encryption)
    if (cJSON_IsString(p256_item) && p256_item->valuestring) {
        push_nvs_set(PUSH_NVS_KEY_P256DH, p256_item->valuestring);
    }
    if (cJSON_IsString(auth_item) && auth_item->valuestring) {
        push_nvs_set(PUSH_NVS_KEY_AUTH, auth_item->valuestring);
    }

    cJSON_Delete(body);
    ESP_LOGI(TAG, "Push subscription stored: %.80s...", ep_item->valuestring);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    return send_json(req, resp, 200);
}

// ── POST /push/notify ─────────────────────────────────────────────────────────

esp_err_t handle_push_notify(httpd_req_t *req)
{
    // Load stored endpoint
    char endpoint[PUSH_ENDPOINT_MAX];
    esp_err_t err = push_nvs_get(PUSH_NVS_KEY_EP, endpoint, sizeof(endpoint));
    if (err != ESP_OK) {
        return send_error(req, 503, "no push subscription stored; POST to /push/subscription first");
    }

    // Parse optional title/body from request
    cJSON *body = read_json_body(req);
    const char *title = "Meatreader Alert";
    const char *msg   = "Temperature alert fired";
    char title_buf[64];
    char msg_buf[128];

    if (body) {
        cJSON *t = cJSON_GetObjectItemCaseSensitive(body, "title");
        cJSON *m = cJSON_GetObjectItemCaseSensitive(body, "body");
        if (cJSON_IsString(t) && t->valuestring) {
            strlcpy(title_buf, t->valuestring, sizeof(title_buf));
            title = title_buf;
        }
        if (cJSON_IsString(m) && m->valuestring) {
            strlcpy(msg_buf, m->valuestring, sizeof(msg_buf));
            msg = msg_buf;
        }
        cJSON_Delete(body);
    }

    // Build a minimal Web Push payload JSON
    // Note: without VAPID + AES-GCM encryption this will be rejected by
    // real push services, but works with custom relay servers.
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "title", title);
    cJSON_AddStringToObject(payload, "body",  msg);
    char *payload_str = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!payload_str) {
        return send_error(req, 500, "out of memory");
    }

    // Make outbound HTTP POST to push endpoint
    esp_http_client_config_t cfg = {
        .url            = endpoint,
        .method         = HTTP_METHOD_POST,
        .timeout_ms     = 10000,
        // For HTTPS push endpoints: enable certificate bundle for verification
        // crt_bundle_attach = esp_crt_bundle_attach,  // requires esp-tls
        // For development with plain HTTP relay servers:
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(payload_str);
        return send_error(req, 500, "failed to init HTTP client");
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload_str, (int)strlen(payload_str));

    err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(payload_str);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "push relay failed: %s", esp_err_to_name(err));
        return send_error(req, 502, "push relay request failed");
    }

    ESP_LOGI(TAG, "push relay -> %s status=%d", endpoint, status);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddNumberToObject(resp, "push_service_status", (double)status);
    return send_json(req, resp, 200);
}
