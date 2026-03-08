// Route handlers: GET /profiles, PUT /profiles/{id}, DELETE /profiles/{id}

#include "http_util.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "route_profiles";

// ── JSON serialization ────────────────────────────────────────────────────────

static const char *alert_method_to_str(uint8_t method)
{
    switch ((alert_method_t)method) {
        case ALERT_METHOD_WEBHOOK: return "webhook";
        case ALERT_METHOD_MQTT:    return "mqtt";
        case ALERT_METHOD_NONE:
        default:                   return "none";
    }
}

static uint8_t str_to_alert_method(const char *s)
{
    if (!s) return (uint8_t)ALERT_METHOD_NONE;
    if (strcmp(s, "webhook") == 0) return (uint8_t)ALERT_METHOD_WEBHOOK;
    if (strcmp(s, "mqtt")    == 0) return (uint8_t)ALERT_METHOD_MQTT;
    return (uint8_t)ALERT_METHOD_NONE;
}

static cJSON *stage_to_json(const stage_t *s)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "target_temp_c", (double)s->target_temp_c);
    cJSON_AddStringToObject(obj, "alert_method",  alert_method_to_str(s->alert_method));
    cJSON_AddStringToObject(obj, "label",         s->label);
    return obj;
}

static cJSON *profile_to_json(int id, const cook_profile_t *p)
{
    cJSON *obj    = cJSON_CreateObject();
    cJSON *stages = cJSON_CreateArray();

    cJSON_AddNumberToObject(obj, "id",         id);
    cJSON_AddStringToObject(obj, "name",       p->name);
    cJSON_AddNumberToObject(obj, "num_stages", p->num_stages);

    int n = p->num_stages < COOK_PROFILE_MAX_STAGES ? p->num_stages : COOK_PROFILE_MAX_STAGES;
    for (int i = 0; i < n; i++) {
        cJSON_AddItemToArray(stages, stage_to_json(&p->stages[i]));
    }
    cJSON_AddItemToObject(obj, "stages", stages);
    return obj;
}

// ── URI ID parsing ────────────────────────────────────────────────────────────

// Extract the trailing integer from a URI like "/profiles/3".
// Returns -1 if the URI has no valid integer suffix.
static int parse_profile_id(const char *uri)
{
    const char *last_slash = strrchr(uri, '/');
    if (!last_slash || last_slash[1] == '\0') return -1;
    char *end;
    long val = strtol(last_slash + 1, &end, 10);
    if (*end != '\0' || val < 0 || val >= COOK_PROFILE_MAX_PROFILES) return -1;
    return (int)val;
}

// ── Handlers ──────────────────────────────────────────────────────────────────

// GET /profiles
// Returns all 8 profile slots as a JSON array. Slots with empty name are
// included so the client always receives a fixed-length array.
esp_err_t handle_profiles_get(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    device_config_t active;
    config_mgr_get_active(ctx->config, &active);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < COOK_PROFILE_MAX_PROFILES; i++) {
        cJSON_AddItemToArray(arr, profile_to_json(i, &active.cook.profiles[i]));
    }

    ESP_LOGD(TAG, "GET /profiles → 200");
    return send_json(req, arr, 200);
}

// PUT /profiles/{id}
// Full replacement of the profile at the given slot. Updates staged and
// immediately applies (staged → active). Body must be a JSON object with at
// least "name" and "stages" fields.
esp_err_t handle_profiles_put(httpd_req_t *req)
{
    int id = parse_profile_id(req->uri);
    if (id < 0) {
        return send_error(req, 400, "invalid profile id (0-7)");
    }

    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    cJSON *body = read_json_body(req);
    if (!body) {
        return send_error(req, 400, "missing or invalid JSON body");
    }

    device_config_t staged;
    config_mgr_get_staged(ctx->config, &staged);

    cook_profile_t *p = &staged.cook.profiles[id];
    memset(p, 0, sizeof(*p));

    cJSON *name = cJSON_GetObjectItemCaseSensitive(body, "name");
    if (cJSON_IsString(name) && name->valuestring) {
        strncpy(p->name, name->valuestring, sizeof(p->name) - 1);
    }

    cJSON *stages = cJSON_GetObjectItemCaseSensitive(body, "stages");
    if (cJSON_IsArray(stages)) {
        int count = cJSON_GetArraySize(stages);
        if (count > COOK_PROFILE_MAX_STAGES) count = COOK_PROFILE_MAX_STAGES;
        p->num_stages = (uint8_t)count;
        for (int i = 0; i < count; i++) {
            cJSON *s = cJSON_GetArrayItem(stages, i);
            if (!cJSON_IsObject(s)) continue;

            cJSON *temp = cJSON_GetObjectItemCaseSensitive(s, "target_temp_c");
            if (cJSON_IsNumber(temp)) {
                p->stages[i].target_temp_c = (float)temp->valuedouble;
            }

            cJSON *method = cJSON_GetObjectItemCaseSensitive(s, "alert_method");
            if (cJSON_IsString(method)) {
                p->stages[i].alert_method = str_to_alert_method(method->valuestring);
            }

            cJSON *label = cJSON_GetObjectItemCaseSensitive(s, "label");
            if (cJSON_IsString(label) && label->valuestring) {
                strncpy(p->stages[i].label, label->valuestring, sizeof(p->stages[i].label) - 1);
            }
        }
    }

    cJSON_Delete(body);

    config_mgr_set_staged(ctx->config, &staged);
    config_mgr_apply(ctx->config);

    device_config_t active;
    config_mgr_get_active(ctx->config, &active);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddItemToObject(resp, "profile", profile_to_json(id, &active.cook.profiles[id]));

    ESP_LOGD(TAG, "PUT /profiles/%d → 200", id);
    return send_json(req, resp, 200);
}

// DELETE /profiles/{id}
// Clears the profile slot (zeroes name and stages). Updates staged and applies.
esp_err_t handle_profiles_delete(httpd_req_t *req)
{
    int id = parse_profile_id(req->uri);
    if (id < 0) {
        return send_error(req, 400, "invalid profile id (0-7)");
    }

    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    device_config_t staged;
    config_mgr_get_staged(ctx->config, &staged);

    memset(&staged.cook.profiles[id], 0, sizeof(cook_profile_t));

    config_mgr_set_staged(ctx->config, &staged);
    config_mgr_apply(ctx->config);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddNumberToObject(resp, "deleted_id", id);

    ESP_LOGD(TAG, "DELETE /profiles/%d → 200", id);
    return send_json(req, resp, 200);
}
