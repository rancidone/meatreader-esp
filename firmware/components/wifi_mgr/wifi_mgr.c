#include "wifi_mgr.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi_mgr";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MAX_RECONNECT       5

static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t       *s_netif            = NULL;
static int                s_retry_count      = 0;
static char               s_ip_str[16]       = "0.0.0.0";
static bool               s_connected        = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry_count < MAX_RECONNECT) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "Reconnecting... (%d/%d)", s_retry_count, MAX_RECONNECT);
        } else {
            ESP_LOGW(TAG, "Max reconnect attempts reached");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        s_connected   = true;
        s_retry_count = 0;
        ESP_LOGI(TAG, "Got IP: %s", s_ip_str);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_mgr_init(void)
{
    if (s_wifi_event_group) return ESP_OK;

    s_wifi_event_group = xEventGroupCreate();
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    return ESP_OK;
}

esp_err_t wifi_mgr_connect(const char *ssid, const char *password)
{
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid,     ssid,     sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    s_retry_count = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    return esp_wifi_start();
}

esp_err_t wifi_mgr_wait_connected(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,   // don't clear on exit
        pdFALSE,   // wait for any bit
        pdMS_TO_TICKS(timeout_ms)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

bool wifi_mgr_is_connected(void)
{
    return s_connected;
}

const char *wifi_mgr_get_ip(void)
{
    return s_ip_str;
}

void wifi_mgr_deinit(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_netif) {
        esp_netif_destroy(s_netif);
        s_netif = NULL;
    }
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
}
