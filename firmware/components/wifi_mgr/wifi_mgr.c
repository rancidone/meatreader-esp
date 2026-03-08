#include "wifi_mgr.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <string.h>

static const char *TAG = "wifi_mgr";

// ── STA state ────────────────────────────────────────────────────────────────

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MAX_RECONNECT       5

static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t       *s_netif            = NULL;
static int                s_retry_count      = 0;
static char               s_ip_str[16]       = "0.0.0.0";
static bool               s_connected        = false;
static bool               s_softap_mode      = false;

// ── DNS hijack task ──────────────────────────────────────────────────────────
// Listens on UDP port 53; responds to every query with an A record pointing
// to 192.168.4.1, triggering the captive portal popup on all major OSes.

#define DNS_PORT        53
#define DNS_BUF_SIZE    512
#define SOFTAP_IP       0xC0A80401u   // 192.168.4.1 in network byte order

static void dns_hijack_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket create failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(DNS_PORT),
    };

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS socket bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS hijack task running on port %d", DNS_PORT);

    static uint8_t buf[DNS_BUF_SIZE];
    while (1) {
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);

        int len = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&client, &client_len);
        if (len < 12) continue;  // too short to be a valid DNS header

        // Build a minimal DNS response. The answer section is a single A record
        // pointing to 192.168.4.1. We echo the question section verbatim.

        // Header: flags → response (QR=1, AA=1, RA=1), RCODE=0
        buf[2] = 0x81;
        buf[3] = 0x80;
        // QD count stays the same (already 1); AN count = 1
        buf[6] = 0x00;
        buf[7] = 0x01;
        // NS count = 0, AR count = 0
        buf[8] = buf[9] = buf[10] = buf[11] = 0;

        // Append answer after the original message.
        // Name: pointer to question name (offset 12)
        int resp_len = len;
        if (resp_len + 16 > (int)sizeof(buf)) {
            // oversized query — skip
            continue;
        }
        buf[resp_len++] = 0xC0;
        buf[resp_len++] = 0x0C;     // name pointer → offset 12
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x01;     // type A
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x01;     // class IN
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x00;     // TTL = 0 (don't cache)
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x04;     // RDLENGTH = 4
        // RDATA: 192.168.4.1
        buf[resp_len++] = 192;
        buf[resp_len++] = 168;
        buf[resp_len++] = 4;
        buf[resp_len++] = 1;

        sendto(sock, buf, resp_len, 0,
               (struct sockaddr *)&client, client_len);
    }
    // unreachable — task runs forever until device resets
}

// ── STA event handler ────────────────────────────────────────────────────────

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

// ── Public API ───────────────────────────────────────────────────────────────

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

esp_err_t wifi_mgr_start_softap(const char *ap_ssid)
{
    s_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;

    wifi_config_t ap_cfg = {
        .ap = {
            .max_connection = 4,
            .authmode       = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = (uint8_t)strlen(ap_ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_softap_mode = true;
    ESP_LOGI(TAG, "SoftAP started — SSID: %s, IP: 192.168.4.1", ap_ssid);

    // DNS hijack: redirect all queries to 192.168.4.1 so captive portal fires.
    xTaskCreate(dns_hijack_task, "dns_hijack", 3072, NULL, 5, NULL);

    return ESP_OK;
}

bool wifi_mgr_in_softap_mode(void)
{
    return s_softap_mode;
}
