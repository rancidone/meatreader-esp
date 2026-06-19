// Route handlers: all /config/* endpoints

#include "http_util.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "route_config";

// ── Alert method string helpers (shared with routes_alerts.c) ─────────────────

static alert_method_t config_str_to_alert_method(const char *s)
{
    if (!s) return ALERT_METHOD_NONE;
    if (strcmp(s, "webhook") == 0) return ALERT_METHOD_WEBHOOK;
    if (strcmp(s, "mqtt")    == 0) return ALERT_METHOD_MQTT;
    return ALERT_METHOD_NONE;
}

// ── JSON → device_config_t patch ─────────────────────────────────────────────
//
// Applies recognized fields from a JSON patch object to an existing
// device_config_t. Unknown keys are silently ignored (permissive patch).

static void apply_json_patch(device_config_t *cfg, cJSON *patch)
{
    cJSON *item;

    item = cJSON_GetObjectItemCaseSensitive(patch, "wifi_ssid");
    if (cJSON_IsString(item)) {
        strncpy(cfg->wifi_ssid, item->valuestring, CONFIG_WIFI_SSID_MAX - 1);
        cfg->wifi_ssid[CONFIG_WIFI_SSID_MAX - 1] = '\0';
    }

    item = cJSON_GetObjectItemCaseSensitive(patch, "wifi_password");
    if (cJSON_IsString(item)) {
        strncpy(cfg->wifi_password, item->valuestring, CONFIG_WIFI_PASS_MAX - 1);
        cfg->wifi_password[CONFIG_WIFI_PASS_MAX - 1] = '\0';
    }

    item = cJSON_GetObjectItemCaseSensitive(patch, "sample_rate_hz");
    if (cJSON_IsNumber(item)) {
        float rate = (float)item->valuedouble;
        if (rate >= 0.1f && rate <= 10.0f) {
            cfg->sample_rate_hz = rate;
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(patch, "ema_alpha");
    if (cJSON_IsNumber(item)) {
        float alpha = (float)item->valuedouble;
        if (alpha > 0.0f && alpha <= 1.0f) {
            cfg->ema_alpha = alpha;
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(patch, "spike_reject_delta_c");
    if (cJSON_IsNumber(item)) {
        float delta = (float)item->valuedouble;
        if (delta > 0.0f && delta <= 100.0f) {
            cfg->spike_reject_delta_c = delta;
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(patch, "channels");
    if (cJSON_IsArray(item)) {
        int count = cJSON_GetArraySize(item);
        if (count != CONFIG_NUM_CHANNELS) {
            ESP_LOGW(TAG, "patch: channels array must have exactly %d entries, got %d — ignoring",
                     CONFIG_NUM_CHANNELS, count);
        } else {
            for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
                cJSON *ch = cJSON_GetArrayItem(item, i);
                if (!cJSON_IsObject(ch)) continue;

                cJSON *enabled = cJSON_GetObjectItemCaseSensitive(ch, "enabled");
                if (cJSON_IsBool(enabled)) {
                    cfg->channels[i].enabled = cJSON_IsTrue(enabled);
                }

                cJSON *label = cJSON_GetObjectItemCaseSensitive(ch, "label");
                if (cJSON_IsString(label) && label->valuestring) {
                    snprintf(cfg->channels[i].label, CONFIG_CHANNEL_LABEL_MAX,
                             "%s", label->valuestring);
                }

                cJSON *sh = cJSON_GetObjectItemCaseSensitive(ch, "steinhart_hart");
                if (cJSON_IsObject(sh)) {
                    cJSON *a = cJSON_GetObjectItemCaseSensitive(sh, "a");
                    cJSON *b = cJSON_GetObjectItemCaseSensitive(sh, "b");
                    cJSON *c = cJSON_GetObjectItemCaseSensitive(sh, "c");
                    if (cJSON_IsNumber(a) && cJSON_IsNumber(b) && cJSON_IsNumber(c)) {
                        sh_coeffs_t new_sh = {
                            .a = a->valuedouble,
                            .b = b->valuedouble,
                            .c = c->valuedouble,
                        };
                        if (therm_math_sh_valid(&new_sh)) {
                            cfg->channels[i].sh = new_sh;
                        } else {
                            ESP_LOGW(TAG, "ch%d: invalid S-H coefficients in patch — ignoring", i);
                        }
                    }
                }
            }
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(patch, "alerts");
    if (cJSON_IsArray(item)) {
        int count = cJSON_GetArraySize(item);
        if (count != CONFIG_NUM_CHANNELS) {
            ESP_LOGW(TAG, "patch: alerts array must have exactly %d entries, got %d — ignoring",
                     CONFIG_NUM_CHANNELS, count);
        } else {
            for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
                cJSON *entry = cJSON_GetArrayItem(item, i);
                if (!cJSON_IsObject(entry)) continue;

                cJSON *enabled = cJSON_GetObjectItemCaseSensitive(entry, "enabled");
                if (cJSON_IsBool(enabled)) {
                    cfg->alerts[i].enabled = cJSON_IsTrue(enabled);
                }

                cJSON *target = cJSON_GetObjectItemCaseSensitive(entry, "target_temp_c");
                if (cJSON_IsNumber(target)) {
                    float t = (float)target->valuedouble;
                    if (t >= 0.0f && t <= 300.0f) {
                        cfg->alerts[i].target_temp_c = t;
                    }
                }

                cJSON *method = cJSON_GetObjectItemCaseSensitive(entry, "method");
                if (cJSON_IsString(method)) {
                    cfg->alerts[i].method = config_str_to_alert_method(method->valuestring);
                }

                cJSON *url = cJSON_GetObjectItemCaseSensitive(entry, "webhook_url");
                if (cJSON_IsString(url)) {
                    strncpy(cfg->alerts[i].webhook_url, url->valuestring,
                            sizeof(cfg->alerts[i].webhook_url) - 1);
                    cfg->alerts[i].webhook_url[sizeof(cfg->alerts[i].webhook_url) - 1] = '\0';
                }
            }
        }
    }
}

// ── Handlers ──────────────────────────────────────────────────────────────────

// GET /config
esp_err_t handle_config_get(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    device_config_t active;
    config_mgr_get_active(ctx->config, &active);

    ESP_LOGD(TAG, "GET /config → 200");
    return send_json(req, device_config_to_json(&active), 200);
}

// PATCH /config
// Partial update applied directly to the active (running) config.
esp_err_t handle_config_patch(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    cJSON *body = read_json_body(req);
    if (!body) {
        return send_error(req, 400, "missing or invalid JSON body");
    }

    device_config_t active;
    config_mgr_get_active(ctx->config, &active);
    apply_json_patch(&active, body);
    cJSON_Delete(body);
    config_mgr_set_active(ctx->config, &active);

    config_mgr_get_active(ctx->config, &active);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddItemToObject(resp, "config", device_config_to_json(&active));

    ESP_LOGD(TAG, "PATCH /config → 200");
    return send_json(req, resp, 200);
}

// POST /config/commit  (active → NVS flash)
esp_err_t handle_config_commit(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;
    esp_err_t err = config_mgr_commit(ctx->config);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config commit failed: %s", esp_err_to_name(err));
        return send_error(req, 500, "NVS write failed");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status",  "ok");
    cJSON_AddStringToObject(resp, "message", "configuration persisted to flash");

    ESP_LOGD(TAG, "POST /config/commit → 200");
    return send_json(req, resp, 200);
}
