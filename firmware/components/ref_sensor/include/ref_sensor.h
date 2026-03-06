#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef struct ds18b20_dev *ds18b20_handle_t;

// Initialize DS18B20 on the given GPIO pin.
// Configures the pin and verifies device presence via reset pulse.
// Returns ESP_ERR_NOT_FOUND if no device responds, ESP_ERR_NOT_SUPPORTED if
// the implementation is not yet complete (stub state).
esp_err_t ds18b20_init(int gpio_pin, ds18b20_handle_t *out_handle);

// Read temperature in Celsius. Blocks ~750ms (12-bit resolution conversion).
// Returns ESP_FAIL on CRC error or communication failure.
esp_err_t ds18b20_read_celsius(ds18b20_handle_t handle, float *out_temp_c);

void ds18b20_deinit(ds18b20_handle_t handle);
