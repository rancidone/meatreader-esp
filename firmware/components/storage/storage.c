#include "storage.h"
#include "esp_spiffs.h"

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
    if (ret == ESP_OK) {
        s_mounted = true;
    }
    return ret;
}

void storage_fs_deinit(void)
{
    if (s_mounted) {
        esp_vfs_spiffs_unregister("storage");
        s_mounted = false;
    }
}
