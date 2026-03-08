#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ads1115.h"
#include "config_mgr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// Event group bit set after each snapshot update.
// Consumers (e.g. SSE handler) wait on this bit to detect new data.
#define SENSOR_MGR_SNAPSHOT_BIT  (1 << 0)

typedef enum {
    SENSOR_QUALITY_OK       = 0,
    SENSOR_QUALITY_ERROR    = 1,  // I2C failure or out-of-range result
    SENSOR_QUALITY_DISABLED = 2,  // channel disabled in config
} sensor_quality_t;

typedef struct {
    int              channel_id;
    float            temperature_c;   // EMA-filtered temperature
    float            raw_adc;         // raw ADS1115 count (float for JSON)
    float            resistance_ohms; // computed thermistor resistance
    sensor_quality_t quality;
} channel_reading_t;

typedef struct {
    int64_t          timestamp_ms;    // ms since boot (esp_timer_get_time / 1000)
    channel_reading_t channels[CONFIG_NUM_CHANNELS];
} sensor_snapshot_t;

typedef struct sensor_mgr sensor_mgr_t;

// Initialize sensor manager. Does not start the sampling task.
esp_err_t sensor_mgr_init(ads1115_handle_t ads, config_mgr_t *config,
                            sensor_mgr_t **out);

// Start the background sampling task. Call once after init.
esp_err_t sensor_mgr_start(sensor_mgr_t *mgr);

// Copy the latest snapshot. Returns false if no data is available yet
// (task has not completed a first sample).
bool sensor_mgr_get_latest(sensor_mgr_t *mgr, sensor_snapshot_t *out_snap);

// Return the FreeRTOS event group used to signal snapshot updates.
// Bit 0 (SENSOR_MGR_SNAPSHOT_BIT) is set after each snapshot write.
EventGroupHandle_t sensor_mgr_get_event_group(sensor_mgr_t *mgr);

void sensor_mgr_deinit(sensor_mgr_t *mgr);
