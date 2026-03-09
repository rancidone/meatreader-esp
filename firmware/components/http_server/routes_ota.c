// routes_ota.c — OTA firmware update endpoints
//
//   POST /ota
//     Receives a raw .bin firmware image as the request body.
//     Writes it to the inactive OTA partition, then sets it as the boot
//     partition and restarts. The app must call
//     esp_ota_mark_app_valid_cancel_rollback() on successful start
//     (done in wifi_mgr after a successful WiFi connection).
//
//     On failure the running partition is unchanged — the device stays live.
//
//   POST /ota/rollback
//     Marks the currently running partition as invalid and reboots to the
//     previous partition (must have been a valid OTA partition).

#include "http_util.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "route_ota";

// Minimum sane firmware size: ESP32 image header + one section header (≥ 32 B).
// We use 64 KB as a practical lower bound — real firmware is always larger.
#define OTA_MIN_IMAGE_BYTES  (64 * 1024)

// Chunk size for streaming reads from the HTTP body.
#define OTA_RECV_CHUNK       4096

// POST /ota
esp_err_t handle_ota_upload(httpd_req_t *req)
{
    int content_len = req->content_len;

    if (content_len <= 0) {
        return send_error(req, 400, "Content-Length required");
    }
    if (content_len < OTA_MIN_IMAGE_BYTES) {
        ESP_LOGW(TAG, "Rejected: image too small (%d bytes)", content_len);
        return send_error(req, 400, "firmware image too small (minimum 64 KB)");
    }

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        ESP_LOGE(TAG, "No OTA update partition found — is TWO_OTA enabled?");
        return send_error(req, 500, "no OTA partition available");
    }

    ESP_LOGI(TAG, "OTA update → partition '%s' (offset 0x%08" PRIx32 ", size %" PRIu32 " B)",
             update_part->label, update_part->address, update_part->size);

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return send_error(req, 500, "OTA begin failed");
    }

    char *buf = malloc(OTA_RECV_CHUNK);
    if (!buf) {
        esp_ota_abort(ota_handle);
        return send_error(req, 500, "out of memory");
    }

    int remaining = content_len;
    bool first_chunk = true;

    while (remaining > 0) {
        int to_read = (remaining < OTA_RECV_CHUNK) ? remaining : OTA_RECV_CHUNK;
        int received = httpd_req_recv(req, buf, to_read);

        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                // Transient — try again
                continue;
            }
            ESP_LOGE(TAG, "httpd_req_recv error: %d", received);
            free(buf);
            esp_ota_abort(ota_handle);
            return send_error(req, 400, "upload interrupted");
        }

        // Validate magic byte in the first chunk
        if (first_chunk) {
            first_chunk = false;
            if ((uint8_t)buf[0] != ESP_IMAGE_HEADER_MAGIC) {
                ESP_LOGW(TAG, "Invalid image magic: 0x%02x (expected 0x%02x)",
                         (uint8_t)buf[0], ESP_IMAGE_HEADER_MAGIC);
                free(buf);
                esp_ota_abort(ota_handle);
                return send_error(req, 400, "not a valid ESP32 firmware image");
            }
        }

        err = esp_ota_write(ota_handle, buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_abort(ota_handle);
            return send_error(req, 500, "OTA write failed");
        }

        remaining -= received;
    }

    free(buf);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        return send_error(req, 500, "OTA verification failed");
    }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return send_error(req, 500, "OTA set boot partition failed");
    }

    ESP_LOGI(TAG, "OTA complete — rebooting to '%s'", update_part->label);

    // Send response before restarting
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "message", "firmware flashed — rebooting");
    send_json(req, resp, 200);

    // Brief delay so the response can be flushed
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK;  // unreachable
}

// POST /ota/rollback
esp_err_t handle_ota_rollback(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *last_invalid = esp_ota_get_last_invalid_partition();

    if (!last_invalid && running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        // Check if there's another OTA partition available for rollback
        esp_ota_img_states_t ota0_state, ota1_state;
        const esp_partition_t *ota0 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
        const esp_partition_t *ota1 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);

        bool can_rollback = false;
        if (ota0 && esp_ota_get_state_partition(ota0, &ota0_state) == ESP_OK &&
            ota1 && esp_ota_get_state_partition(ota1, &ota1_state) == ESP_OK) {
            can_rollback = (ota0_state == ESP_OTA_IMG_VALID || ota1_state == ESP_OTA_IMG_VALID);
        }

        if (!can_rollback) {
            return send_error(req, 409, "no previous partition available for rollback");
        }
    }

    ESP_LOGI(TAG, "OTA rollback requested — marking current partition invalid and rebooting");

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "message", "rolling back — rebooting");
    send_json(req, resp, 200);

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_ota_mark_app_invalid_rollback_and_reboot();

    return ESP_OK;  // unreachable
}
