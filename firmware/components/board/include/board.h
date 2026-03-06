#pragma once

// ── I2C ──────────────────────────────────────────────────────────────────────
#define BOARD_I2C_SDA       21
#define BOARD_I2C_SCL       22
#define BOARD_I2C_PORT      0
#define BOARD_I2C_SPEED_HZ  400000

// ── ADS1115 ───────────────────────────────────────────────────────────────────
#define BOARD_ADS1115_ADDR  0x48  // ADDR pin tied to GND

// ── DS18B20 ───────────────────────────────────────────────────────────────────
#define BOARD_DS18B20_PIN   13

// ── Thermistor circuit ────────────────────────────────────────────────────────
//
//   VCC ─── R_TOP ─── ADC_IN ─── R_SERIES ─── R_THERMISTOR ─── GND
//
// ADC measures the voltage across (R_SERIES + R_THERMISTOR).
// Solving the divider: R_therm = (V_adc * R_top) / (Vref - V_adc) - R_series
//
// These must match the actual hardware. If changed, run a new calibration.
#define BOARD_R_TOP_OHMS       22100.0f  // R3: top resistor (VCC to ADC)
#define BOARD_R_SERIES_OHMS      470.0f  // R1: series protection (ADC to thermistor)

// ── ADS1115 PGA and reference ────────────────────────────────────────────────
// PGA set to ±4.096V (register value 0x01 << 9 in config register).
// Vref is the supply rail driving the divider.
// ADC_MAX is the signed 16-bit maximum for the ADS1115.
#define BOARD_ADS_VFS          4.096f   // full-scale voltage for selected PGA
#define BOARD_ADC_VREF         3.3f     // divider supply voltage
#define BOARD_ADC_MAX          32767.0f // ADS1115 max count (16-bit signed)

// ── Channel count ─────────────────────────────────────────────────────────────
#define BOARD_NUM_CHANNELS     2
