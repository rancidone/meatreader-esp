#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Mount SPIFFS at /spiffs (partition label "storage", max 10 open files).
// Returns ESP_OK on success; on failure the filesystem is unavailable but
// boot continues — callers must tolerate a missing mount.
esp_err_t storage_fs_init(void);

// Unmount SPIFFS. Safe to call even if storage_fs_init() failed.
void storage_fs_deinit(void);

// Returns true if SPIFFS is currently mounted.
bool storage_fs_mounted(void);
