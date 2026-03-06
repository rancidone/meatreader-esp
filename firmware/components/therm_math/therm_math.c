#include "therm_math.h"
#include <math.h>
#include <stdbool.h>

// Coefficients from Toit prototype: approximate for 100K NTC thermistor.
// Source: resistance_model.toit defaults.
const sh_coeffs_t THERM_MATH_DEFAULT_COEFFS = {
    .a = 7.322291889e-4,
    .b = 2.132158182e-4,
    .c = 1.148231681e-7,
};

bool therm_math_sh_valid(const sh_coeffs_t *coeffs)
{
    return (coeffs->a > 0.0 && coeffs->b > 0.0 && coeffs->c > 0.0);
}

float therm_math_adc_to_resistance(int16_t raw_adc,
                                    float r_top, float r_series,
                                    float vfs, float vref, float adc_max)
{
    float v = ((float)raw_adc / adc_max) * vfs;
    float denom = vref - v;

    if (fabsf(denom) < 1.0e-4f) {
        return 1.0e9f;  // near-zero denominator → open circuit / ADC rail
    }

    float r = (v * r_top) / denom - r_series;

    if (r <= 0.0f) {
        return 1.0e9f;  // physically impossible → likely wiring issue
    }

    return r;
}

float therm_math_resistance_to_celsius(float resistance_ohms, const sh_coeffs_t *coeffs)
{
    // Use double precision — S-H coefficients are tiny (e-4 to e-7 range)
    // and ln(R) can reach ~11, so float accumulates significant error.
    double ln_r  = log((double)resistance_ohms);
    double ln_r3 = ln_r * ln_r * ln_r;
    double temp_k = 1.0 / (coeffs->a + coeffs->b * ln_r + coeffs->c * ln_r3);
    return (float)(temp_k - 273.15);
}

float therm_math_ema_update(float state, float new_value, float alpha)
{
    if (isnanf(state)) {
        return new_value;  // first sample: seed filter with raw value
    }
    return alpha * new_value + (1.0f - alpha) * state;
}
