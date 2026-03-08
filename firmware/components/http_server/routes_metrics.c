// Route handler: GET /metrics — Prometheus text format exposition.

#include "http_util.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "route_metrics";

// Buffer large enough for all channels + scalar metrics.
// 4 channels × ~3 metrics × ~80 bytes each + headers + scalars ≈ 1.5 KB; 2 KB is safe.
#define METRICS_BUF_SIZE 2048

esp_err_t handle_metrics(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    sensor_snapshot_t snap;
    bool has_data = sensor_mgr_get_latest(ctx->sensor, &snap);

    static char buf[METRICS_BUF_SIZE];
    int off = 0;

#define APPEND(...) \
    do { \
        int _n = snprintf(buf + off, METRICS_BUF_SIZE - off, __VA_ARGS__); \
        if (_n > 0) off += _n; \
    } while (0)

    // ── meatreader_temperature_celsius ────────────────────────────────────────
    APPEND("# HELP meatreader_temperature_celsius Current temperature per channel\n");
    APPEND("# TYPE meatreader_temperature_celsius gauge\n");
    if (has_data) {
        for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
            const channel_reading_t *ch = &snap.channels[i];
            // Fetch label from active config.
            APPEND("meatreader_temperature_celsius{channel=\"%d\",label=\"\"} %.2f\n",
                   i, ch->temperature_c);
        }
    }

    // ── meatreader_resistance_ohms ────────────────────────────────────────────
    APPEND("# HELP meatreader_resistance_ohms Thermistor resistance per channel\n");
    APPEND("# TYPE meatreader_resistance_ohms gauge\n");
    if (has_data) {
        for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
            APPEND("meatreader_resistance_ohms{channel=\"%d\"} %.1f\n",
                   i, snap.channels[i].resistance_ohms);
        }
    }

    // ── meatreader_adc_raw ────────────────────────────────────────────────────
    APPEND("# HELP meatreader_adc_raw Raw ADC reading per channel\n");
    APPEND("# TYPE meatreader_adc_raw gauge\n");
    if (has_data) {
        for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
            APPEND("meatreader_adc_raw{channel=\"%d\"} %.0f\n",
                   i, snap.channels[i].raw_adc);
        }
    }

    // ── meatreader_wifi_rssi_dbm ──────────────────────────────────────────────
    int rssi = 0;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }
    APPEND("# HELP meatreader_wifi_rssi_dbm WiFi RSSI in dBm\n");
    APPEND("# TYPE meatreader_wifi_rssi_dbm gauge\n");
    APPEND("meatreader_wifi_rssi_dbm %d\n", rssi);

    // ── meatreader_uptime_seconds ─────────────────────────────────────────────
    int64_t uptime_s = esp_timer_get_time() / 1000000LL;
    APPEND("# HELP meatreader_uptime_seconds Device uptime in seconds\n");
    APPEND("# TYPE meatreader_uptime_seconds counter\n");
    APPEND("meatreader_uptime_seconds %lld\n", uptime_s);

    // ── meatreader_channel_quality ────────────────────────────────────────────
    APPEND("# HELP meatreader_channel_quality Channel reading quality (1=ok, 0=degraded)\n");
    APPEND("# TYPE meatreader_channel_quality gauge\n");
    if (has_data) {
        for (int i = 0; i < CONFIG_NUM_CHANNELS; i++) {
            int quality_ok = (snap.channels[i].quality == SENSOR_QUALITY_OK) ? 1 : 0;
            APPEND("meatreader_channel_quality{channel=\"%d\"} %d\n", i, quality_ok);
        }
    }

#undef APPEND

    ESP_LOGD(TAG, "GET /metrics → 200 (%d bytes)", off);

    httpd_resp_set_type(req, "text/plain; version=0.0.4; charset=utf-8");
    return httpd_resp_sendstr(req, buf);
}
