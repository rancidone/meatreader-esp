#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdint.h>

typedef struct ads1115_dev *ads1115_handle_t;

// Initialize ADS1115. Adds a device to the I2C bus and verifies communication.
// addr: 7-bit I2C address (0x48–0x4B based on ADDR pin wiring).
// Returns ESP_ERR_NOT_FOUND if the device does not respond.
esp_err_t ads1115_init(i2c_master_bus_handle_t bus, uint8_t addr,
                        ads1115_handle_t *out_handle);

// Read a single-ended channel (0–3) using single-shot mode.
// Blocks for one conversion period (~12ms at 128 SPS).
// Returns the raw signed 16-bit ADC count.
esp_err_t ads1115_read_channel(ads1115_handle_t handle, int channel,
                                int16_t *out_raw);

void ads1115_deinit(ads1115_handle_t handle);
