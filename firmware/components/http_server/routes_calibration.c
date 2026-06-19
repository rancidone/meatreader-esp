// Route handlers: all /calibration/* endpoints
//
// All session endpoints operate on all channels simultaneously.
// A single DS18B20 read is shared across channels on each capture.

#include "http_util.h"
#include "esp_log.h"

static const char *TAG = "route_cal";

// GET /calibration/live
// Returns DS18B20 reference temperature and current readings for all channels.
esp_err_t handle_cal_live(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    float ref_temp_c = 0.0f;
    bool  has_ref    = false;

    if (ctx->ds18 != NULL) {
        esp_err_t err = ds18b20_read_celsius(ctx->ds18, &ref_temp_c);
        if (err == ESP_OK) {
            has_ref = true;
        } else {
            ESP_LOGW(TAG, "cal/live: DS18B20 read failed: %s", esp_err_to_name(err));
        }
    }

    sensor_snapshot_t snap;
    bool has_snap = sensor_mgr_get_latest(ctx->sensor, &snap);

    cJSON *obj      = cJSON_CreateObject();
    cJSON *channels = cJSON_CreateArray();

    if (has_ref) {
        cJSON_AddNumberToObject(obj, "reference_temp_c", ref_temp_c);
        cJSON_AddNumberToObject(obj, "reference_temp_f", ref_temp_c * 9.0f / 5.0f + 32.0f);
    } else {
        cJSON_AddNullToObject(obj, "reference_temp_c");
        cJSON_AddNullToObject(obj, "reference_temp_f");
    }

    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        cJSON *ch = cJSON_CreateObject();
        cJSON_AddNumberToObject(ch, "channel", i);

        if (has_snap) {
            const channel_reading_t *cr = &snap.channels[i];
            const char *quality_str;
            switch (cr->quality) {
                case SENSOR_QUALITY_OK:       quality_str = "ok";       break;
                case SENSOR_QUALITY_ERROR:    quality_str = "error";    break;
                case SENSOR_QUALITY_DISABLED: quality_str = "disabled"; break;
                default:                      quality_str = "unknown";  break;
            }
            cJSON_AddNumberToObject(ch, "thermistor_temp_c", cr->temperature_c);
            cJSON_AddNumberToObject(ch, "thermistor_temp_f", cr->temperature_c * 9.0f / 5.0f + 32.0f);
            cJSON_AddNumberToObject(ch, "raw_adc",           cr->raw_adc);
            cJSON_AddStringToObject(ch, "quality",           quality_str);
        } else {
            cJSON_AddNullToObject(ch, "thermistor_temp_c");
            cJSON_AddNullToObject(ch, "thermistor_temp_f");
            cJSON_AddNullToObject(ch, "raw_adc");
            cJSON_AddStringToObject(ch, "quality", "no_data");
        }

        cJSON_AddItemToArray(channels, ch);
    }

    cJSON_AddItemToObject(obj, "channels", channels);
    return send_json(req, obj, 200);
}

// POST /calibration/session/start
// Resets calibration sessions for all channels.
esp_err_t handle_cal_session_start(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        esp_err_t err = calibration_start_session(ctx->calibration, i);
        if (err != ESP_OK) {
            return send_error(req, 500, "failed to start session");
        }
    }

    cJSON *resp     = cJSON_CreateObject();
    cJSON *sessions = cJSON_CreateArray();
    cJSON_AddStringToObject(resp, "status", "ok");

    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        cal_session_t s;
        calibration_get_session(ctx->calibration, i, &s);
        cJSON_AddItemToArray(sessions, cal_session_to_json(&s));
    }
    cJSON_AddItemToObject(resp, "sessions", sessions);

    return send_json(req, resp, 200);
}

// POST /calibration/point/capture
// Reads DS18B20 once, then records that reference temp + each channel's raw ADC
// as a calibration point for all channels simultaneously.
esp_err_t handle_cal_point_capture(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    if (ctx->ds18 == NULL) {
        return send_error(req, 503,
            "DS18B20 not available — hardware not detected at boot");
    }

    float ref_temp_c = 0.0f;
    esp_err_t err = ds18b20_read_celsius(ctx->ds18, &ref_temp_c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS18B20 read failed: %s", esp_err_to_name(err));
        return send_error(req, 500, "DS18B20 read failed");
    }

    sensor_snapshot_t snap;
    if (!sensor_mgr_get_latest(ctx->sensor, &snap)) {
        return send_error(req, 503, "no sensor data yet");
    }

    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        if (snap.channels[i].quality == SENSOR_QUALITY_ERROR) {
            ESP_LOGW(TAG, "ch%d: quality=error, skipping capture", i);
            continue;
        }
        err = calibration_add_point(ctx->calibration, i, ref_temp_c,
                                     snap.channels[i].raw_adc);
        if (err == ESP_ERR_INVALID_STATE) {
            return send_error(req, 409,
                "no active calibration session — call /session/start first");
        }
        if (err != ESP_OK) {
            return send_error(req, 500, "failed to add calibration point");
        }
    }

    ESP_LOGI(TAG, "captured point ref=%.2f°C/%.2f°F for all channels",
             ref_temp_c, ref_temp_c * 9.0f / 5.0f + 32.0f);

    cJSON *resp     = cJSON_CreateObject();
    cJSON *sessions = cJSON_CreateArray();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddNumberToObject(resp, "reference_temp_c", ref_temp_c);
    cJSON_AddNumberToObject(resp, "reference_temp_f", ref_temp_c * 9.0f / 5.0f + 32.0f);

    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        cal_session_t s;
        calibration_get_session(ctx->calibration, i, &s);
        cJSON_AddItemToArray(sessions, cal_session_to_json(&s));
    }
    cJSON_AddItemToObject(resp, "sessions", sessions);

    return send_json(req, resp, 200);
}

// POST /calibration/solve
// Fits S-H coefficients for all channels and returns them without committing.
esp_err_t handle_cal_solve(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    cJSON *resp    = cJSON_CreateObject();
    cJSON *results = cJSON_CreateArray();
    cJSON_AddStringToObject(resp, "status", "ok");

    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        sh_coeffs_t coeffs;
        esp_err_t err = calibration_solve(ctx->calibration, i, &coeffs);

        cJSON *ch = cJSON_CreateObject();
        cJSON_AddNumberToObject(ch, "channel", i);

        if (err == ESP_ERR_INVALID_STATE) {
            cJSON_AddStringToObject(ch, "status", "error");
            cJSON_AddStringToObject(ch, "error", "no active session or fewer than 3 points");
        } else if (err != ESP_OK) {
            cJSON_AddStringToObject(ch, "status", "error");
            cJSON_AddStringToObject(ch, "error",
                "solver failed — points may not span a wide enough range");
        } else {
            cJSON_AddStringToObject(ch, "status", "ok");
            cJSON_AddItemToObject(ch, "coefficients", sh_coeffs_to_json(&coeffs));
        }
        cJSON_AddItemToArray(results, ch);
    }

    cJSON_AddItemToObject(resp, "channels", results);
    return send_json(req, resp, 200);
}

// POST /calibration/accept
// Solves all channels and writes coefficients to active config.
// Call POST /config/commit afterwards to persist to flash.
esp_err_t handle_cal_accept(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    cJSON *resp    = cJSON_CreateObject();
    cJSON *results = cJSON_CreateArray();
    bool   any_ok  = false;

    for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
        sh_coeffs_t coeffs;
        esp_err_t err = calibration_solve(ctx->calibration, i, &coeffs);

        cJSON *ch = cJSON_CreateObject();
        cJSON_AddNumberToObject(ch, "channel", i);

        if (err == ESP_ERR_INVALID_STATE) {
            cJSON_AddStringToObject(ch, "status", "error");
            cJSON_AddStringToObject(ch, "error", "no active session or fewer than 3 points");
        } else if (err != ESP_OK) {
            cJSON_AddStringToObject(ch, "status", "error");
            cJSON_AddStringToObject(ch, "error", "solver failed");
        } else {
            calibration_accept(ctx->calibration, i, &coeffs);
            cJSON_AddStringToObject(ch, "status", "ok");
            cJSON_AddItemToObject(ch, "coefficients", sh_coeffs_to_json(&coeffs));
            any_ok = true;
        }
        cJSON_AddItemToArray(results, ch);
    }

    cJSON_AddStringToObject(resp, "status", any_ok ? "ok" : "error");
    cJSON_AddStringToObject(resp, "message",
        any_ok ? "coefficients written to active config — call /config/commit to persist"
               : "all channels failed to solve");
    cJSON_AddItemToObject(resp, "channels", results);

    return send_json(req, resp, any_ok ? 200 : 400);
}
