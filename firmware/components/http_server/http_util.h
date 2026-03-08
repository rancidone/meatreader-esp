// http_util.h — internal utilities for HTTP route handlers.
// Not in include/ — not part of the public component API.
#pragma once

#include "esp_http_server.h"
#include "cJSON.h"
#include "http_server.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ── Response helpers ──────────────────────────────────────────────────────────

// Send a JSON object response with the given HTTP status code.
// Takes ownership of obj and calls cJSON_Delete after sending.
static inline esp_err_t send_json(httpd_req_t *req, cJSON *obj, int status_code)
{
    char status_str[12];
    snprintf(status_str, sizeof(status_str), "%d", status_code);

    // esp_http_server only accepts string status lines, not just codes.
    const char *status_line;
    switch (status_code) {
        case 200: status_line = "200 OK";                    break;
        case 400: status_line = "400 Bad Request";           break;
        case 404: status_line = "404 Not Found";             break;
        case 405: status_line = "405 Method Not Allowed";    break;
        case 409: status_line = "409 Conflict";              break;
        case 500: status_line = "500 Internal Server Error"; break;
        case 503: status_line = "503 Service Unavailable";   break;
        default:  status_line = status_str;                  break;
    }

    httpd_resp_set_status(req, status_line);
    httpd_resp_set_type(req, "application/json");

    char *json_str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (!json_str) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    esp_err_t err = httpd_resp_sendstr(req, json_str);
    free(json_str);
    return err;
}

// Send a JSON error response: {"error": "message"}
static inline esp_err_t send_error(httpd_req_t *req, int status_code,
                                    const char *message)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "error", message);
    return send_json(req, obj, status_code);
}

// ── Request helpers ───────────────────────────────────────────────────────────

// Read and parse the JSON body. Returns a cJSON object on success (caller must
// cJSON_Delete), or NULL on empty body / parse error.
static inline cJSON *read_json_body(httpd_req_t *req)
{
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > 4096) return NULL;

    char *buf = malloc(content_len + 1);
    if (!buf) return NULL;

    int received = httpd_req_recv(req, buf, content_len);
    if (received <= 0) {
        free(buf);
        return NULL;
    }
    buf[received] = '\0';

    cJSON *obj = cJSON_Parse(buf);
    free(buf);
    return obj;
}

// Parse an integer query parameter. Returns default_val if not present or invalid.
static inline int query_param_int(httpd_req_t *req, const char *key, int default_val)
{
    char val_str[16];
    if (httpd_req_get_url_query_str(req, NULL, 0) == ESP_ERR_NOT_FOUND) {
        return default_val;
    }

    // Allocate a buffer for the full query string.
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0) return default_val;

    char *query_buf = malloc(qlen + 1);
    if (!query_buf) return default_val;

    esp_err_t err = httpd_req_get_url_query_str(req, query_buf, qlen + 1);
    if (err != ESP_OK) {
        free(query_buf);
        return default_val;
    }

    int result = default_val;
    if (httpd_query_key_value(query_buf, key, val_str, sizeof(val_str)) == ESP_OK) {
        result = atoi(val_str);
    }
    free(query_buf);
    return result;
}

// ── JSON serialization helpers ────────────────────────────────────────────────

#include "sensor_mgr.h"
#include "config_mgr.h"
#include "calibration.h"

// alert may be NULL — if non-NULL, "alert_triggered" is included in the object.
static inline cJSON *channel_reading_to_json(const channel_reading_t *ch,
                                              const channel_alert_t *alert)
{
    const char *quality_str;
    switch (ch->quality) {
        case SENSOR_QUALITY_OK:       quality_str = "ok";       break;
        case SENSOR_QUALITY_ERROR:    quality_str = "error";    break;
        case SENSOR_QUALITY_DISABLED: quality_str = "disabled"; break;
        default:                      quality_str = "unknown";  break;
    }

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id",               ch->channel_id);
    cJSON_AddNumberToObject(obj, "temperature_c",    ch->temperature_c);
    cJSON_AddNumberToObject(obj, "raw_adc",          ch->raw_adc);
    cJSON_AddNumberToObject(obj, "resistance_ohms",  ch->resistance_ohms);
    cJSON_AddStringToObject(obj, "quality",          quality_str);
    if (alert != NULL) {
        cJSON_AddBoolToObject(obj, "alert_triggered", alert->triggered);
    }
    return obj;
}

// alerts is an optional array of CONFIG_NUM_CHANNELS channel_alert_t entries.
// Pass NULL to omit "alert_triggered" from channel objects.
static inline cJSON *snapshot_to_json(const sensor_snapshot_t *snap,
                                       const channel_alert_t *alerts)
{
    cJSON *obj      = cJSON_CreateObject();
    cJSON *channels = cJSON_CreateArray();

    cJSON_AddNumberToObject(obj, "timestamp", (double)snap->timestamp_ms);
    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        const channel_alert_t *alert = alerts ? &alerts[i] : NULL;
        cJSON_AddItemToArray(channels, channel_reading_to_json(&snap->channels[i], alert));
    }
    cJSON_AddItemToObject(obj, "channels", channels);
    return obj;
}

static inline cJSON *sh_coeffs_to_json(const sh_coeffs_t *sh)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "a", sh->a);
    cJSON_AddNumberToObject(obj, "b", sh->b);
    cJSON_AddNumberToObject(obj, "c", sh->c);
    return obj;
}

static inline cJSON *channel_config_to_json(const channel_config_t *ch)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(obj,   "enabled",       ch->enabled);
    cJSON_AddNumberToObject(obj, "adc_channel",   ch->adc_channel);
    cJSON_AddItemToObject(obj,   "steinhart_hart", sh_coeffs_to_json(&ch->sh));
    return obj;
}

static inline cJSON *device_config_to_json(const device_config_t *cfg)
{
    cJSON *obj      = cJSON_CreateObject();
    cJSON *channels = cJSON_CreateArray();

    // Never expose wifi_password — write-only field.
    cJSON_AddStringToObject(obj, "wifi_ssid",       cfg->wifi_ssid);
    cJSON_AddStringToObject(obj, "wifi_password",   "***");
    cJSON_AddNumberToObject(obj, "sample_rate_hz",         cfg->sample_rate_hz);
    cJSON_AddNumberToObject(obj, "ema_alpha",               cfg->ema_alpha);
    cJSON_AddNumberToObject(obj, "spike_reject_delta_c",   cfg->spike_reject_delta_c);

    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        cJSON_AddItemToArray(channels, channel_config_to_json(&cfg->channels[i]));
    }
    cJSON_AddItemToObject(obj, "channels", channels);
    return obj;
}

static inline cJSON *cal_session_to_json(const cal_session_t *s)
{
    cJSON *obj    = cJSON_CreateObject();
    cJSON *points = cJSON_CreateArray();

    cJSON_AddNumberToObject(obj, "channel",    s->channel);
    cJSON_AddBoolToObject(obj,   "active",     s->active);
    cJSON_AddNumberToObject(obj, "num_points", s->num_points);

    for (int i = 0; i < s->num_points; i++) {
        cJSON *pt = cJSON_CreateObject();
        cJSON_AddNumberToObject(pt, "reference_temp_c", s->points[i].ref_temp_c);
        cJSON_AddNumberToObject(pt, "raw_adc",          s->points[i].raw_adc);
        cJSON_AddItemToArray(points, pt);
    }
    cJSON_AddItemToObject(obj, "points", points);
    return obj;
}

