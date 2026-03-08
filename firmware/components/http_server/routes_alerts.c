// Route handlers: GET /alerts, PATCH /alerts/staged

#include "http_util.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "route_alerts";

// ── JSON serialization helpers ────────────────────────────────────────────────

static const char *alert_method_to_str(alert_method_t method)
{
    switch (method) {
        case ALERT_METHOD_WEBHOOK: return "webhook";
        case ALERT_METHOD_MQTT:    return "mqtt";
        case ALERT_METHOD_NONE:
        default:                   return "none";
    }
}

static alert_method_t str_to_alert_method(const char *s)
{
    if (!s) return ALERT_METHOD_NONE;
    if (strcmp(s, "webhook") == 0) return ALERT_METHOD_WEBHOOK;
    if (strcmp(s, "mqtt")    == 0) return ALERT_METHOD_MQTT;
    return ALERT_METHOD_NONE;
}

static cJSON *alert_to_json(int channel, const channel_alert_t *alert)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "channel",       channel);
    cJSON_AddBoolToObject(obj,   "enabled",       alert->enabled);
    cJSON_AddNumberToObject(obj, "target_temp_c", (double)alert->target_temp_c);
    cJSON_AddStringToObject(obj, "method",        alert_method_to_str(alert->method));
    cJSON_AddStringToObject(obj, "webhook_url",   alert->webhook_url);
    cJSON_AddBoolToObject(obj,   "triggered",     alert->triggered);
    return obj;
}

static cJSON *alerts_array_to_json(const device_config_t *cfg)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        cJSON_AddItemToArray(arr, alert_to_json(i, &cfg->alerts[i]));
    }
    return arr;
}

// ── PATCH helper ──────────────────────────────────────────────────────────────

// Apply an alerts JSON array patch onto the alerts[] in cfg.
// Returns false if the array has the wrong number of entries.
static bool apply_alerts_patch(device_config_t *cfg, cJSON *arr)
{
    int count = cJSON_GetArraySize(arr);
    if (count != CONFIG_NUM_CHANNELS) {
        ESP_LOGW(TAG, "alerts array must have exactly %d entries, got %d",
                 CONFIG_NUM_CHANNELS, count);
        return false;
    }

    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        cJSON *entry = cJSON_GetArrayItem(arr, i);
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
            cfg->alerts[i].method = str_to_alert_method(method->valuestring);
        }

        cJSON *url = cJSON_GetObjectItemCaseSensitive(entry, "webhook_url");
        if (cJSON_IsString(url)) {
            strncpy(cfg->alerts[i].webhook_url, url->valuestring,
                    sizeof(cfg->alerts[i].webhook_url) - 1);
            cfg->alerts[i].webhook_url[sizeof(cfg->alerts[i].webhook_url) - 1] = '\0';
        }
    }
    return true;
}

// ── Handlers ──────────────────────────────────────────────────────────────────

// GET /alerts
// Returns the active alert configuration as a JSON array.
esp_err_t handle_alerts_get(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    device_config_t active;
    config_mgr_get_active(ctx->config, &active);

    cJSON *arr = alerts_array_to_json(&active);
    ESP_LOGD(TAG, "GET /alerts → 200");
    return send_json(req, arr, 200);
}

// PATCH /alerts/staged
// Body: JSON array of alert configs. Updates staged alerts and returns the
// full updated staged alerts array.
esp_err_t handle_alerts_patch_staged(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    cJSON *body = read_json_body(req);
    if (!body) {
        return send_error(req, 400, "missing or invalid JSON body");
    }

    if (!cJSON_IsArray(body)) {
        cJSON_Delete(body);
        return send_error(req, 400, "body must be a JSON array of alert configs");
    }

    device_config_t staged;
    config_mgr_get_staged(ctx->config, &staged);

    if (!apply_alerts_patch(&staged, body)) {
        cJSON_Delete(body);
        return send_error(req, 400, "alerts array must have exactly CONFIG_NUM_CHANNELS entries");
    }
    cJSON_Delete(body);

    config_mgr_set_staged(ctx->config, &staged);

    // Re-read staged to return actual post-patch state.
    config_mgr_get_staged(ctx->config, &staged);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddItemToObject(resp, "staged_alerts", alerts_array_to_json(&staged));

    ESP_LOGD(TAG, "PATCH /alerts/staged → 200");
    return send_json(req, resp, 200);
}
