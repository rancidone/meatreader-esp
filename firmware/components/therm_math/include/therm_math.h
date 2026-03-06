#pragma once

#include <stdint.h>
#include <stdbool.h>

// Steinhart-Hart equation: 1/T = A + B·ln(R) + C·ln(R)³
// All three coefficients must be positive for a valid NTC thermistor curve.
typedef struct {
    double a;
    double b;
    double c;
} sh_coeffs_t;

// Default S-H coefficients for a 100K NTC thermistor.
// These are approximate — run a calibration for accurate results.
extern const sh_coeffs_t THERM_MATH_DEFAULT_COEFFS;

// Returns true if all three S-H coefficients are positive (physically valid).
bool therm_math_sh_valid(const sh_coeffs_t *coeffs);

// Convert raw ADS1115 count to thermistor resistance in ohms.
//
// Circuit:  VCC ─── r_top ─── ADC_IN ─── r_series ─── Rtherm ─── GND
//
// Parameters should come from board.h constants to ensure sensor_mgr and
// calibration use the same model. Mismatched parameters are Bug #1 in the
// original Toit prototype.
//
// Returns 1e9f (open circuit sentinel) if the denominator is near zero or
// the computed resistance is non-positive.
float therm_math_adc_to_resistance(int16_t raw_adc,
                                    float r_top,    // e.g. BOARD_R_TOP_OHMS
                                    float r_series,  // e.g. BOARD_R_SERIES_OHMS
                                    float vfs,       // e.g. BOARD_ADS_VFS
                                    float vref,      // e.g. BOARD_ADC_VREF
                                    float adc_max);  // e.g. BOARD_ADC_MAX

// Convert resistance in ohms to temperature in Celsius via Steinhart-Hart.
// Uses double precision internally for numerical stability.
float therm_math_resistance_to_celsius(float resistance_ohms, const sh_coeffs_t *coeffs);

// Exponential moving average update.
//   state:     previous EMA value. Pass NAN (from <math.h>) on first call.
//   new_value: latest raw sample.
//   alpha:     smoothing factor in (0, 1]. Higher = tracks faster.
// Returns the updated EMA value. If state is NaN, returns new_value directly.
float therm_math_ema_update(float state, float new_value, float alpha);
