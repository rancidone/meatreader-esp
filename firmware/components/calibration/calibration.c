#include "calibration.h"
#include "therm_math.h"
#include "config_mgr.h"
#include "board.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "calibration";

struct calibration_mgr {
    config_mgr_t *config;
    cal_session_t sessions[CONFIG_NUM_CHANNELS];
};

// ── Solver ────────────────────────────────────────────────────────────────────
//
// Fits Steinhart-Hart coefficients via ordinary least squares on:
//   1/T_K = A + B·ln(R) + C·ln(R)³
//
// This reduces to solving XᵀX · [A, B, C]ᵀ = Xᵀy where:
//   X[i] = [1,  ln(R_i),  ln(R_i)³]
//   y[i] = 1 / T_K_i
//
// Solved with Cramer's rule (exact for 3×3; stable for well-spread points).
//
// KEY FIX vs Toit prototype: R is computed via therm_math_adc_to_resistance()
// using the same BOARD_* constants as the runtime pipeline. The Toit version
// used a hardcoded 100kΩ divider here but 22.1kΩ at runtime, making calibration
// produce coefficients tuned to the wrong circuit model.

// Standard cofactor expansion along the first row.
static double det3(double m[3][3])
{
    return m[0][0] * (m[1][1]*m[2][2] - m[1][2]*m[2][1])
         - m[0][1] * (m[1][0]*m[2][2] - m[1][2]*m[2][0])
         + m[0][2] * (m[1][0]*m[2][1] - m[1][1]*m[2][0]);
}

static esp_err_t fit_steinhart_hart(const cal_point_t *points, int n,
                                     sh_coeffs_t *out)
{
    // Allocate design matrix and y vector on heap (n can be up to CAL_MAX_POINTS).
    double (*X)[3] = calloc(n, sizeof(*X));
    double  *y     = calloc(n, sizeof(*y));
    if (!X || !y) {
        free(X); free(y);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < n; i++) {
        float r = therm_math_adc_to_resistance(
            (int16_t)points[i].raw_adc,
            BOARD_R_TOP_OHMS, BOARD_R_SERIES_OHMS,
            BOARD_ADS_VFS, BOARD_ADC_VREF, BOARD_ADC_MAX
        );

        if (r <= 0.0f || r >= 1.0e8f) {
            ESP_LOGE(TAG, "point %d: resistance out of range (%.1f ohms)", i, r);
            free(X); free(y);
            return ESP_FAIL;
        }

        double ln_r = log((double)r);
        X[i][0] = 1.0;
        X[i][1] = ln_r;
        X[i][2] = ln_r * ln_r * ln_r;
        y[i]    = 1.0 / ((double)points[i].ref_temp_c + 273.15);
    }

    // Compute XᵀX (3×3, row-major) and Xᵀy (3-vector).
    // (XᵀX)[r][c] = Σ_k X[k][r] * X[k][c]
    double XtX[3][3] = {{0}};
    double Xty[3]    = {0};

    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            for (int k = 0; k < n; k++) {
                XtX[r][c] += X[k][r] * X[k][c];
            }
        }
        for (int k = 0; k < n; k++) {
            Xty[r] += X[k][r] * y[k];
        }
    }

    free(X);
    free(y);

    double d = det3(XtX);
    if (fabs(d) < 1.0e-20) {
        ESP_LOGE(TAG, "Singular matrix — points may be too similar or too few");
        return ESP_FAIL;
    }

    // Cramer's rule: β[col] = det(XtX with column col replaced by Xty) / d
    double coeffs[3];
    for (int col = 0; col < 3; col++) {
        double M[3][3];
        memcpy(M, XtX, sizeof(M));
        for (int row = 0; row < 3; row++) {
            M[row][col] = Xty[row];
        }
        coeffs[col] = det3(M) / d;
    }

    // All three coefficients must be positive for a valid NTC thermistor fit.
    if (coeffs[0] <= 0.0 || coeffs[1] <= 0.0 || coeffs[2] <= 0.0) {
        ESP_LOGE(TAG, "Solver produced non-positive coefficients (a=%.3e b=%.3e c=%.3e) "
                 "— check that reference temperatures span a wide enough range",
                 coeffs[0], coeffs[1], coeffs[2]);
        return ESP_FAIL;
    }

    out->a = coeffs[0];
    out->b = coeffs[1];
    out->c = coeffs[2];

    ESP_LOGI(TAG, "S-H solved: a=%.6e  b=%.6e  c=%.6e", out->a, out->b, out->c);
    return ESP_OK;
}

// ── Public API ────────────────────────────────────────────────────────────────

esp_err_t calibration_init(config_mgr_t *config, calibration_mgr_t **out)
{
    struct calibration_mgr *mgr = calloc(1, sizeof(*mgr));
    if (!mgr) return ESP_ERR_NO_MEM;
    mgr->config = config;
    *out = mgr;
    return ESP_OK;
}

esp_err_t calibration_start_session(calibration_mgr_t *mgr, int channel)
{
    if (channel < 0 || channel >= CONFIG_NUM_CHANNELS) return ESP_ERR_INVALID_ARG;
    cal_session_t *s = &mgr->sessions[channel];
    memset(s, 0, sizeof(*s));
    s->active  = true;
    s->channel = channel;
    ESP_LOGI(TAG, "Calibration session started for ch%d", channel);
    return ESP_OK;
}

esp_err_t calibration_add_point(calibration_mgr_t *mgr, int channel,
                                  float ref_temp_c, float raw_adc)
{
    if (channel < 0 || channel >= CONFIG_NUM_CHANNELS) return ESP_ERR_INVALID_ARG;
    cal_session_t *s = &mgr->sessions[channel];
    if (!s->active) {
        ESP_LOGE(TAG, "ch%d: no active session", channel);
        return ESP_ERR_INVALID_STATE;
    }
    if (s->num_points >= CAL_MAX_POINTS) {
        ESP_LOGE(TAG, "ch%d: session full (%d points)", channel, CAL_MAX_POINTS);
        return ESP_ERR_NO_MEM;
    }

    s->points[s->num_points].ref_temp_c = ref_temp_c;
    s->points[s->num_points].raw_adc    = raw_adc;
    s->num_points++;

    ESP_LOGI(TAG, "ch%d: captured point %d  ref=%.2f°C  adc=%.0f",
             channel, s->num_points, ref_temp_c, raw_adc);
    return ESP_OK;
}

esp_err_t calibration_get_session(calibration_mgr_t *mgr, int channel,
                                    cal_session_t *out)
{
    if (channel < 0 || channel >= CONFIG_NUM_CHANNELS) return ESP_ERR_INVALID_ARG;
    *out = mgr->sessions[channel];
    return ESP_OK;
}

esp_err_t calibration_solve(calibration_mgr_t *mgr, int channel,
                              sh_coeffs_t *out_coeffs)
{
    if (channel < 0 || channel >= CONFIG_NUM_CHANNELS) return ESP_ERR_INVALID_ARG;
    cal_session_t *s = &mgr->sessions[channel];
    if (!s->active) {
        ESP_LOGE(TAG, "ch%d: no active session", channel);
        return ESP_ERR_INVALID_STATE;
    }
    if (s->num_points < CAL_MIN_POINTS) {
        ESP_LOGE(TAG, "ch%d: need at least %d points, have %d",
                 channel, CAL_MIN_POINTS, s->num_points);
        return ESP_ERR_INVALID_STATE;
    }
    return fit_steinhart_hart(s->points, s->num_points, out_coeffs);
}

esp_err_t calibration_accept(calibration_mgr_t *mgr, int channel,
                               const sh_coeffs_t *coeffs)
{
    if (channel < 0 || channel >= CONFIG_NUM_CHANNELS) return ESP_ERR_INVALID_ARG;

    device_config_t staged;
    config_mgr_get_staged(mgr->config, &staged);
    staged.channels[channel].sh = *coeffs;
    config_mgr_set_staged(mgr->config, &staged);

    ESP_LOGI(TAG, "ch%d: calibration coefficients written to staged config", channel);
    return ESP_OK;
}

void calibration_deinit(calibration_mgr_t *mgr)
{
    free(mgr);
}
