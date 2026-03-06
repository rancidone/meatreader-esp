#include "i2c_manager.h"
#include "board.h"
#include "esp_log.h"

static const char *TAG = "i2c_mgr";

static i2c_master_bus_handle_t s_bus = NULL;

esp_err_t i2c_manager_init(void)
{
    if (s_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t cfg = {
        .i2c_port            = BOARD_I2C_PORT,
        .sda_io_num          = BOARD_I2C_SDA,
        .scl_io_num          = BOARD_I2C_SCL,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C bus init: SDA=%d SCL=%d @ %dkHz",
             BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_SPEED_HZ / 1000);
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_manager_get_bus(void)
{
    return s_bus;
}

void i2c_manager_deinit(void)
{
    if (s_bus != NULL) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
}
