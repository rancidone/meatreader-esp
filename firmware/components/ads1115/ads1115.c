#include "ads1115.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char *TAG = "ads1115";

// ── Register addresses ────────────────────────────────────────────────────────
#define REG_CONVERSION   0x00
#define REG_CONFIG       0x01

// ── Config register bit fields ────────────────────────────────────────────────
// Bit 15   OS:   1 = start single conversion (write), 1 = idle (read)
// Bits 14-12 MUX: channel select (single-ended vs GND)
// Bits 11-9  PGA: programmable gain
// Bit 8    MODE: 1 = single-shot
// Bits 7-5  DR:  data rate
// Bits 1-0  COMP_QUE: 11 = disable comparator

#define CFG_OS            (1u << 15)
#define CFG_PGA_4096V     (0x01u << 9)   // ±4.096V — matches BOARD_ADS_VFS
#define CFG_MODE_SINGLE   (1u << 8)
#define CFG_DR_128SPS     (0x04u << 5)   // 128 samples/sec → ~7.8ms/conversion
#define CFG_COMP_DISABLE  0x0003u

// Single-ended MUX values for channels 0–3 (AINx vs GND)
static const uint16_t CFG_MUX_SINGLE[4] = {
    0x04u << 12,  // AIN0 vs GND
    0x05u << 12,  // AIN1 vs GND
    0x06u << 12,  // AIN2 vs GND
    0x07u << 12,  // AIN3 vs GND
};

// Conversion time guard: 128 SPS → 7.8ms. Add margin for I2C overhead.
#define CONVERSION_WAIT_MS  12

struct ads1115_dev {
    i2c_master_dev_handle_t i2c_dev;
};

esp_err_t ads1115_init(i2c_master_bus_handle_t bus, uint8_t addr,
                        ads1115_handle_t *out_handle)
{
    struct ads1115_dev *dev = calloc(1, sizeof(*dev));
    if (!dev) return ESP_ERR_NO_MEM;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 400000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev->i2c_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device at 0x%02X: %s", addr, esp_err_to_name(err));
        free(dev);
        return err;
    }

    // Probe: write a known config and verify the device ACKs.
    uint16_t config = CFG_OS | CFG_MUX_SINGLE[0] | CFG_PGA_4096V |
                      CFG_MODE_SINGLE | CFG_DR_128SPS | CFG_COMP_DISABLE;
    uint8_t write_buf[3] = {
        REG_CONFIG,
        (uint8_t)(config >> 8),
        (uint8_t)(config & 0xFF),
    };

    err = i2c_master_transmit(dev->i2c_dev, write_buf, sizeof(write_buf), 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADS1115 not responding at 0x%02X: %s", addr, esp_err_to_name(err));
        i2c_master_bus_rm_device(dev->i2c_dev);
        free(dev);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "ADS1115 initialized at I2C 0x%02X (±4.096V PGA, 128 SPS)", addr);
    *out_handle = dev;
    return ESP_OK;
}

esp_err_t ads1115_read_channel(ads1115_handle_t handle, int channel, int16_t *out_raw)
{
    if (channel < 0 || channel > 3) return ESP_ERR_INVALID_ARG;

    // Start single-shot conversion on the selected channel.
    uint16_t config = CFG_OS | CFG_MUX_SINGLE[channel] | CFG_PGA_4096V |
                      CFG_MODE_SINGLE | CFG_DR_128SPS | CFG_COMP_DISABLE;
    uint8_t write_buf[3] = {
        REG_CONFIG,
        (uint8_t)(config >> 8),
        (uint8_t)(config & 0xFF),
    };

    esp_err_t err = i2c_master_transmit(handle->i2c_dev, write_buf, sizeof(write_buf), 100);
    if (err != ESP_OK) return err;

    // Wait for conversion. Polling the OS bit is more precise but 12ms delay
    // is simpler and safe for 128 SPS. TODO: poll OS bit if lower latency needed.
    vTaskDelay(pdMS_TO_TICKS(CONVERSION_WAIT_MS));

    // Point to conversion register, then read 2 bytes.
    uint8_t reg = REG_CONVERSION;
    uint8_t read_buf[2];
    err = i2c_master_transmit_receive(handle->i2c_dev, &reg, 1, read_buf, 2, 100);
    if (err != ESP_OK) return err;

    *out_raw = (int16_t)((read_buf[0] << 8) | read_buf[1]);
    return ESP_OK;
}

void ads1115_deinit(ads1115_handle_t handle)
{
    if (handle) {
        i2c_master_bus_rm_device(handle->i2c_dev);
        free(handle);
    }
}
