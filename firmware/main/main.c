// Meatreader ESP32 — application entry point.
//
// Boot sequence:
//   1. NVS flash init
//   2. TCP/IP and event loop init
//   3. Board + I2C bus init
//   4. Config load from NVS (falls back to defaults)
//   5. ADS1115 init (fatal on failure)
//   6. DS18B20 init (optional; logs warning on failure)
//   7. Sensor manager init + start
//   8. WiFi connect + wait (non-fatal timeout)
//   9. HTTP server start

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"

#include "board.h"
#include "i2c_manager.h"
#include "ads1115.h"
#include "ref_sensor.h"
#include "config_mgr.h"
#include "sensor_mgr.h"
#include "calibration.h"
#include "wifi_mgr.h"
#include "http_server.h"

static const char *TAG = "main";

// WiFi connect timeout before proceeding to HTTP start anyway.
#define WIFI_CONNECT_TIMEOUT_MS  15000

void app_main(void)
{
    ESP_LOGI(TAG, "Meatreader ESP32 — booting");

    // ── NVS ───────────────────────────────────────────────────────────────
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition issue (%s), erasing and reinitializing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // ── Networking init (required before WiFi) ────────────────────────────
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // ── I2C bus ───────────────────────────────────────────────────────────
    ESP_ERROR_CHECK(i2c_manager_init());
    ESP_LOGI(TAG, "I2C bus ready");

    // ── Config ────────────────────────────────────────────────────────────
    config_mgr_t *config = NULL;
    ESP_ERROR_CHECK(config_mgr_init(&config));

    // ── ADS1115 (fatal) ───────────────────────────────────────────────────
    ads1115_handle_t ads = NULL;
    err = ads1115_init(i2c_manager_get_bus(), BOARD_ADS1115_ADDR, &ads);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADS1115 init failed: %s — cannot sample thermistors, halting",
                 esp_err_to_name(err));
        // In production firmware, a fatal hardware fault should restart and
        // allow the watchdog to handle recovery, not spin forever.
        esp_restart();
    }
    ESP_LOGI(TAG, "ADS1115 ready at I2C 0x%02X", BOARD_ADS1115_ADDR);

    // ── DS18B20 (optional) ────────────────────────────────────────────────
    ds18b20_handle_t ds18 = NULL;
    err = ds18b20_init(BOARD_DS18B20_PIN, &ds18);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20 not available (pin %d): %s — calibration capture disabled",
                 BOARD_DS18B20_PIN, esp_err_to_name(err));
        ds18 = NULL;
    } else {
        ESP_LOGI(TAG, "DS18B20 ready on GPIO %d", BOARD_DS18B20_PIN);
    }

    // ── Sensor manager ────────────────────────────────────────────────────
    sensor_mgr_t *sensor = NULL;
    ESP_ERROR_CHECK(sensor_mgr_init(ads, config, &sensor));
    ESP_ERROR_CHECK(sensor_mgr_start(sensor));
    ESP_LOGI(TAG, "Sensor task started");

    // ── Calibration ───────────────────────────────────────────────────────
    calibration_mgr_t *cal = NULL;
    ESP_ERROR_CHECK(calibration_init(config, &cal));

    // ── WiFi ──────────────────────────────────────────────────────────────
    device_config_t cfg;
    config_mgr_get_active(config, &cfg);

    ESP_ERROR_CHECK(wifi_mgr_init());
    if (strlen(cfg.wifi_ssid) > 0) {
        ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", cfg.wifi_ssid);
        ESP_ERROR_CHECK(wifi_mgr_connect(cfg.wifi_ssid, cfg.wifi_password));
        err = wifi_mgr_wait_connected(WIFI_CONNECT_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WiFi connect timed out — starting HTTP server anyway");
        } else {
            ESP_LOGI(TAG, "WiFi connected, IP: %s", wifi_mgr_get_ip());
        }
    } else {
        ESP_LOGW(TAG, "No WiFi SSID configured — set via PATCH /config/staged + apply + commit");
    }

    // ── HTTP server ───────────────────────────────────────────────────────
    http_app_ctx_t http_ctx = {
        .sensor      = sensor,
        .config      = config,
        .calibration = cal,
        .ds18        = ds18,
    };
    ESP_ERROR_CHECK(http_server_start(&http_ctx));
    ESP_LOGI(TAG, "HTTP server started on port 80");

    // app_main must not return; park it.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
