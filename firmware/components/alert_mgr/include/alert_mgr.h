#pragma once

#include "esp_err.h"
#include "config_mgr.h"
#include "sensor_mgr.h"

typedef struct alert_mgr alert_mgr_t;

// Initialize alert manager. Starts an internal FreeRTOS task that polls the
// sensor manager each second and fires alerts when thresholds are crossed.
// config and sensor must remain valid for the lifetime of alert_mgr.
esp_err_t alert_mgr_init(config_mgr_t *config, sensor_mgr_t *sensor,
                          alert_mgr_t **out);

// Check a snapshot against the active alert config and update triggered state.
// Called internally by the alert task; may also be called externally.
void alert_mgr_check_snapshot(alert_mgr_t *mgr, const sensor_snapshot_t *snap);

// Return the current stall_detected flag for the given channel.
// Returns false for out-of-range channel indices.
bool alert_mgr_get_stall_detected(alert_mgr_t *mgr, int channel);

// Stop the alert task and free all resources.
void alert_mgr_deinit(alert_mgr_t *mgr);
