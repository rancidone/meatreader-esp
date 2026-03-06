// Route handlers: all /calibration/* endpoints

#include "http_util.h"
#include "esp_log.h"

static const char *TAG = "route_cal";

// GET /calibration/live?ch=N
// Returns the current DS18B20 reference temperature (if available) and the
// thermistor reading for the selected channel. Used to monitor stability
// before capturing a calibration point.
esp_err_t handle_cal_live(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;
    int ch = query_param_int(req, "ch", 0);

    // Read DS18B20 reference (blocks ~750ms if available).
    float    ref_temp_c  = 0.0f;
    bool     has_ref     = false;

    if (ctx->ds18 != NULL) {
        esp_err_t err = ds18b20_read_celsius(ctx->ds18, &ref_temp_c);
        if (err == ESP_OK) {
            has_ref = true;
        } else {
            ESP_LOGW(TAG, "cal/live: DS18B20 read failed: %s", esp_err_to_name(err));
        }
    }

    // Pull current thermistor reading for this channel from the snapshot.
    sensor_snapshot_t snap;
    float    therm_temp_c    = 0.0f;
    float    raw_adc         = 0.0f;
    const char *quality_str  = "no_data";
    bool     has_snap        = sensor_mgr_get_latest(ctx->sensor, &snap);

    if (has_snap && ch >= 0 && ch < CONFIG_NUM_CHANNELS) {
        const channel_reading_t *cr = &snap.channels[ch];
        therm_temp_c = cr->temperature_c;
        raw_adc      = cr->raw_adc;
        switch (cr->quality) {
            case SENSOR_QUALITY_OK:       quality_str = "ok";       break;
            case SENSOR_QUALITY_ERROR:    quality_str = "error";    break;
            case SENSOR_QUALITY_DISABLED: quality_str = "disabled"; break;
            default: quality_str = "unknown"; break;
        }
    }

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "channel",            ch);
    if (has_ref) {
        cJSON_AddNumberToObject(obj, "reference_temp_c", ref_temp_c);
    } else {
        cJSON_AddNullToObject(obj, "reference_temp_c");
    }
    cJSON_AddNumberToObject(obj, "thermistor_temp_c",  therm_temp_c);
    cJSON_AddNumberToObject(obj, "raw_adc",            raw_adc);
    cJSON_AddStringToObject(obj, "quality",            quality_str);

    return send_json(req, obj, 200);
}

// POST /calibration/session/start?ch=N
esp_err_t handle_cal_session_start(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;
    int ch = query_param_int(req, "ch", 0);

    esp_err_t err = calibration_start_session(ctx->calibration, ch);
    if (err == ESP_ERR_INVALID_ARG) {
        return send_error(req, 400, "invalid channel");
    }
    if (err != ESP_OK) {
        return send_error(req, 500, "failed to start session");
    }

    cal_session_t session;
    calibration_get_session(ctx->calibration, ch, &session);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddItemToObject(resp, "session", cal_session_to_json(&session));

    return send_json(req, resp, 200);
}

// POST /calibration/point/capture?ch=N
// Reads DS18B20 reference temperature and the current raw ADC for the channel,
// then records the pair as a calibration point.
esp_err_t handle_cal_point_capture(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;
    int ch = query_param_int(req, "ch", 0);

    if (ctx->ds18 == NULL) {
        return send_error(req, 503,
            "DS18B20 not available — hardware not detected at boot");
    }

    // Read reference temperature (blocks ~750ms).
    float ref_temp_c = 0.0f;
    esp_err_t err = ds18b20_read_celsius(ctx->ds18, &ref_temp_c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS18B20 read failed: %s", esp_err_to_name(err));
        return send_error(req, 500, "DS18B20 read failed");
    }

    // Get raw ADC for this channel from the latest snapshot.
    sensor_snapshot_t snap;
    if (!sensor_mgr_get_latest(ctx->sensor, &snap)) {
        return send_error(req, 503, "no sensor data yet");
    }
    if (ch < 0 || ch >= CONFIG_NUM_CHANNELS) {
        return send_error(req, 400, "invalid channel");
    }

    float raw_adc = snap.channels[ch].raw_adc;
    if (snap.channels[ch].quality == SENSOR_QUALITY_ERROR) {
        return send_error(req, 503, "channel has quality=error — cannot capture point");
    }

    err = calibration_add_point(ctx->calibration, ch, ref_temp_c, raw_adc);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, 409, "no active calibration session — call /session/start first");
    }
    if (err != ESP_OK) {
        return send_error(req, 500, "failed to add calibration point");
    }

    cal_session_t session;
    calibration_get_session(ctx->calibration, ch, &session);

    ESP_LOGI(TAG, "ch%d: captured point ref=%.2f°C adc=%.0f", ch, ref_temp_c, raw_adc);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddItemToObject(resp, "session", cal_session_to_json(&session));

    return send_json(req, resp, 200);
}

// POST /calibration/solve?ch=N
// Fits S-H coefficients to the captured points and returns them.
// Does NOT write them to config — use /accept for that.
esp_err_t handle_cal_solve(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;
    int ch = query_param_int(req, "ch", 0);

    sh_coeffs_t coeffs;
    esp_err_t err = calibration_solve(ctx->calibration, ch, &coeffs);

    if (err == ESP_ERR_INVALID_ARG) {
        return send_error(req, 400, "invalid channel");
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, 409, "no active session or fewer than 3 points captured");
    }
    if (err != ESP_OK) {
        return send_error(req, 400,
            "solver failed — check that points span a wide enough temperature range "
            "and that resistances are in a valid range");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status",  "ok");
    cJSON_AddNumberToObject(resp, "channel", ch);
    cJSON_AddItemToObject(resp, "coefficients", sh_coeffs_to_json(&coeffs));

    return send_json(req, resp, 200);
}

// POST /calibration/accept?ch=N
// Solves and writes the resulting S-H coefficients to staged config.
// Caller should then PATCH/apply/commit as desired.
esp_err_t handle_cal_accept(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;
    int ch = query_param_int(req, "ch", 0);

    sh_coeffs_t coeffs;
    esp_err_t err = calibration_solve(ctx->calibration, ch, &coeffs);

    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, 409, "no active session or fewer than 3 points captured");
    }
    if (err != ESP_OK) {
        return send_error(req, 400, "solver failed — see /calibration/solve for details");
    }

    err = calibration_accept(ctx->calibration, ch, &coeffs);
    if (err != ESP_OK) {
        return send_error(req, 500, "failed to write coefficients to staged config");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status",  "ok");
    cJSON_AddStringToObject(resp, "message",
        "coefficients written to staged config — call /config/apply then /config/commit to persist");
    cJSON_AddItemToObject(resp, "coefficients", sh_coeffs_to_json(&coeffs));

    return send_json(req, resp, 200);
}
