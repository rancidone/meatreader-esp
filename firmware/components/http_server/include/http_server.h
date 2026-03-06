#pragma once

#include "esp_err.h"
#include "sensor_mgr.h"
#include "config_mgr.h"
#include "calibration.h"
#include "ref_sensor.h"

#define FIRMWARE_VERSION  "esp-idf-rev1"

// Context passed to every HTTP route handler via req->user_ctx.
typedef struct {
    sensor_mgr_t      *sensor;
    config_mgr_t      *config;
    calibration_mgr_t *calibration;
    ds18b20_handle_t   ds18;        // NULL if DS18B20 not available
} http_app_ctx_t;

// Start the HTTP server and register all URI handlers.
// ctx must remain valid for the lifetime of the server.
esp_err_t http_server_start(const http_app_ctx_t *ctx);

void http_server_stop(void);
