// DS18B20 reference-sensor driver.
//
// Wraps espressif/onewire_bus + espressif/ds18b20 (v0.2.0) managed components.
// Single-sensor, single-bus implementation used as a calibration temperature
// reference.  gpio_pin should have a 4.7 kΩ pull-up to VCC; the internal weak
// pull-up is enabled as a fallback but is insufficient for long cable runs.
//
// ds18b20_trigger_temperature_conversion() already delays ~800 ms internally
// (per the managed component implementation), so no extra wait is needed here.

#include "ref_sensor.h"

#include "esp_log.h"
#include "onewire_bus.h"   // espressif/onewire_bus: bus handle, RMT init, primitives
#include "ds18b20.h"       // espressif/ds18b20: device handle, trigger, get_temperature

#include <stdlib.h>

static const char *TAG = "ref_sensor";

struct ds18b20_dev {
    onewire_bus_handle_t    bus;
    ds18b20_device_handle_t sensor;
};

esp_err_t ds18b20_init(int gpio_pin, ds18b20_handle_t *out_handle)
{
    if (!out_handle) return ESP_ERR_INVALID_ARG;

    onewire_bus_config_t bus_cfg = {
        .bus_gpio_num = gpio_pin,
        .flags = { .en_pull_up = true },
    };
    onewire_bus_rmt_config_t rmt_cfg = {
        .max_rx_bytes = 10,  // 1B ROM command + 8B ROM number + 1B device command
    };

    onewire_bus_handle_t bus = NULL;
    esp_err_t err = onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "1-Wire bus init failed on GPIO %d: %s",
                 gpio_pin, esp_err_to_name(err));
        return err;
    }

    // Single-device bus: no ROM enumeration required.
    // ds18b20_new_device_from_bus issues Skip ROM (0xCC) for all subsequent
    // commands and defaults to 12-bit resolution.
    ds18b20_config_t ds_cfg = {};
    ds18b20_device_handle_t sensor = NULL;
    err = ds18b20_new_device_from_bus(bus, &ds_cfg, &sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS18B20 not found on GPIO %d: %s",
                 gpio_pin, esp_err_to_name(err));
        onewire_bus_del(bus);
        return err;
    }

    // Verify the device is actually present by attempting a bus reset.
    err = onewire_bus_reset(bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No device presence pulse on GPIO %d: %s",
                 gpio_pin, esp_err_to_name(err));
        ds18b20_del_device(sensor);
        onewire_bus_del(bus);
        return ESP_ERR_NOT_FOUND;
    }

    struct ds18b20_dev *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        ds18b20_del_device(sensor);
        onewire_bus_del(bus);
        return ESP_ERR_NO_MEM;
    }
    dev->bus    = bus;
    dev->sensor = sensor;

    ESP_LOGI(TAG, "DS18B20 ready on GPIO %d", gpio_pin);
    *out_handle = dev;
    return ESP_OK;
}

esp_err_t ds18b20_read_celsius(ds18b20_handle_t handle, float *out_temp_c)
{
    if (!handle || !out_temp_c) return ESP_ERR_INVALID_ARG;

    // Trigger conversion and wait for it to complete (~800 ms at 12-bit).
    esp_err_t err = ds18b20_trigger_temperature_conversion(handle->sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Trigger conversion failed: %s", esp_err_to_name(err));
        return err;
    }

    err = ds18b20_get_temperature(handle->sensor, out_temp_c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read temperature failed: %s", esp_err_to_name(err));
    }
    return err;
}

void ds18b20_deinit(ds18b20_handle_t handle)
{
    if (!handle) return;
    ds18b20_del_device(handle->sensor);
    onewire_bus_del(handle->bus);
    free(handle);
}
