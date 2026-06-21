#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

// Initialize the I2C master bus using the pin assignments from board.h.
// Safe to call multiple times; subsequent calls are no-ops.
esp_err_t i2c_manager_init(void);

// Return the bus handle. Must be called after i2c_manager_init().
// Drivers use this to add their device with i2c_master_bus_add_device().
i2c_master_bus_handle_t i2c_manager_get_bus(void);

// Reset the I2C bus (toggles SCL to flush a stuck slave).
// Safe to call from any task when repeated I2C errors are detected.
esp_err_t i2c_manager_reset_bus(void);

void i2c_manager_deinit(void);
