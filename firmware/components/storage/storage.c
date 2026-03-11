#include "storage.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "storage";

static bool s_mounted = false;

esp_err_t storage_fs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/spiffs",
        .partition_label        = "storage",
        .max_files              = 10,
        .format_if_mount_failed = false,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "SPIFFS partition 'storage' not found — check partition table");
        } else if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "SPIFFS mount failed — filesystem may be corrupt");
        } else {
            ESP_LOGW(TAG, "SPIFFS mount failed (%s) — static UI unavailable", esp_err_to_name(ret));
        }
        return ret;
    }

    s_mounted = true;

    size_t total = 0, used = 0;
    if (esp_spiffs_info("storage", &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted: %u KB total, %u KB used", (unsigned)(total / 1024), (unsigned)(used / 1024));
    } else {
        ESP_LOGI(TAG, "SPIFFS mounted (capacity unavailable)");
    }
    return ESP_OK;
}

void storage_fs_deinit(void)
{
    if (s_mounted) {
        esp_vfs_spiffs_unregister("storage");
        s_mounted = false;
    }
}

bool storage_fs_mounted(void)
{
    return s_mounted;
}
