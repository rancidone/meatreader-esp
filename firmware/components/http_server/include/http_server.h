#pragma once

#include "esp_err.h"
#include "sensor_mgr.h"
#include "config_mgr.h"
#include "calibration.h"
#include "ref_sensor.h"
#include "alert_mgr.h"

#define FIRMWARE_VERSION  "esp-idf-rev1"

// Context passed to every HTTP route handler via req->user_ctx.
typedef struct {
    sensor_mgr_t      *sensor;       // NULL in provisioning mode
    config_mgr_t      *config;
    calibration_mgr_t *calibration;  // NULL in provisioning mode
    ds18b20_handle_t   ds18;         // NULL if DS18B20 not available
    alert_mgr_t       *alert_mgr;    // NULL in provisioning mode
    bool               provisioning; // true when running in SoftAP/captive-portal mode
} http_app_ctx_t;

// Start the HTTP server and register all URI handlers.
// If ctx->provisioning is true, only the captive portal routes are registered.
// ctx must remain valid for the lifetime of the server.
esp_err_t http_server_start(const http_app_ctx_t *ctx);

void http_server_stop(void);
