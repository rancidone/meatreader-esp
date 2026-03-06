#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "therm_math.h"
#include "config_mgr.h"

#define CAL_MAX_POINTS  20
#define CAL_MIN_POINTS   3

typedef struct {
    float ref_temp_c;  // DS18B20 reference temperature
    float raw_adc;     // ADS1115 raw count (stored as float; fits int16_t range)
} cal_point_t;

typedef struct {
    bool        active;
    int         channel;
    cal_point_t points[CAL_MAX_POINTS];
    int         num_points;
} cal_session_t;

typedef struct calibration_mgr calibration_mgr_t;

esp_err_t calibration_init(config_mgr_t *config, calibration_mgr_t **out);

// Start (or reset) a calibration session for the given channel.
esp_err_t calibration_start_session(calibration_mgr_t *mgr, int channel);

// Add a capture point. Returns ESP_ERR_INVALID_STATE if no active session.
esp_err_t calibration_add_point(calibration_mgr_t *mgr, int channel,
                                  float ref_temp_c, float raw_adc);

// Read session state (for serializing to JSON in route handlers).
esp_err_t calibration_get_session(calibration_mgr_t *mgr, int channel,
                                    cal_session_t *out);

// Fit Steinhart-Hart coefficients to the captured points.
// Returns ESP_ERR_INVALID_STATE if no session or fewer than CAL_MIN_POINTS.
// Returns ESP_FAIL if the matrix is singular or the result is physically invalid.
esp_err_t calibration_solve(calibration_mgr_t *mgr, int channel,
                              sh_coeffs_t *out_coeffs);

// Write the accepted coefficients into the staged config.
esp_err_t calibration_accept(calibration_mgr_t *mgr, int channel,
                               const sh_coeffs_t *coeffs);

void calibration_deinit(calibration_mgr_t *mgr);
