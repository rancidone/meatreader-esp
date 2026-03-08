// Route handlers for provisioning (SoftAP / captive portal) mode.
//
// Active only when wifi_mgr_in_softap_mode() is true. The main HTTP server
// registers these routes instead of the normal sensor/config routes.
//
// Captive portal flow:
//   1. Device boots in SoftAP mode ("Meatreader-Setup").
//   2. Phone connects; OS fires a captive portal probe (generate_204, etc.).
//   3. This server redirects those probes to http://192.168.4.1/, which serves
//      the inline HTML page.
//   4. User enters SSID/password and submits. POST /provision/connect saves
//      credentials to NVS and schedules a reboot.
//   5. Device reboots, finds credentials, connects in STA mode, starts normally.

#include "http_util.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "route_provision";

// ── Inline captive portal HTML ────────────────────────────────────────────────
// Self-contained: no external scripts, fonts, or images. Fits well under 4 KB.

static const char PROVISION_HTML[] =
    "<!DOCTYPE html>"
    "<html lang='en'>"
    "<head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Meatreader Setup</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;max-width:400px;margin:40px auto;padding:0 16px;color:#222}"
    "h1{font-size:1.4em;margin-bottom:4px}p.sub{color:#666;margin-top:0}"
    "label{display:block;margin-top:16px;font-weight:600;font-size:.9em}"
    "input{width:100%;box-sizing:border-box;padding:10px;border:1px solid #ccc;"
          "border-radius:6px;font-size:1em;margin-top:4px}"
    "button{width:100%;margin-top:20px;padding:12px;background:#c0392b;color:#fff;"
           "border:none;border-radius:6px;font-size:1em;cursor:pointer}"
    "button:disabled{background:#aaa}"
    "#msg{margin-top:16px;padding:12px;border-radius:6px;display:none;font-size:.9em}"
    ".ok{background:#d4edda;color:#155724}.err{background:#f8d7da;color:#721c24}"
    "</style>"
    "</head>"
    "<body>"
    "<h1>&#x1F321; Meatreader Setup</h1>"
    "<p class='sub'>Connect to your WiFi network to continue.</p>"
    "<form id='f'>"
    "<label for='s'>Network name (SSID)</label>"
    "<input id='s' name='ssid' type='text' autocomplete='off' "
           "autocorrect='off' spellcheck='false' required>"
    "<label for='p'>Password</label>"
    "<input id='p' name='password' type='password' autocomplete='current-password'>"
    "<button type='submit' id='btn'>Connect</button>"
    "</form>"
    "<div id='msg'></div>"
    "<script>"
    "document.getElementById('f').addEventListener('submit',async function(e){"
    "e.preventDefault();"
    "var btn=document.getElementById('btn');"
    "var msg=document.getElementById('msg');"
    "btn.disabled=true;btn.textContent='Connecting...';"
    "msg.style.display='none';"
    "try{"
    "var r=await fetch('/provision/connect',{method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({ssid:document.getElementById('s').value,"
    "password:document.getElementById('p').value})});"
    "var d=await r.json();"
    "if(d.error){throw new Error(d.error);}"
    "msg.className='ok';msg.style.display='block';"
    "msg.textContent='Credentials saved! The device is rebooting. "
    "Reconnect to your WiFi and visit the device on your network.';"
    "}catch(ex){"
    "msg.className='err';msg.style.display='block';"
    "msg.textContent='Error: '+ex.message;"
    "btn.disabled=false;btn.textContent='Connect';"
    "}"
    "});"
    "</script>"
    "</body></html>";

// ── Reboot helper ─────────────────────────────────────────────────────────────
// Reboots after a short delay to allow the HTTP response to reach the client.

static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

// ── Handlers ──────────────────────────────────────────────────────────────────

// GET / — serve the captive portal page
esp_err_t handle_provision_root(httpd_req_t *req)
{
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, PROVISION_HTML);
}

// Android: GET /generate_204
// iOS/macOS: GET /hotspot-detect.html
// Windows: GET /ncsi.txt
// All redirect to the portal root.
esp_err_t handle_captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_sendstr(req, "");
    return ESP_OK;
}

// POST /provision/connect
// Body: {"ssid":"...","password":"..."}
// Validates, saves to NVS via config_mgr, schedules reboot.
esp_err_t handle_provision_connect(httpd_req_t *req)
{
    http_app_ctx_t *ctx = (http_app_ctx_t *)req->user_ctx;

    cJSON *body = read_json_body(req);
    if (!body) {
        return send_error(req, 400, "invalid JSON body");
    }

    const cJSON *ssid_item = cJSON_GetObjectItemCaseSensitive(body, "ssid");
    const cJSON *pass_item = cJSON_GetObjectItemCaseSensitive(body, "password");

    if (!cJSON_IsString(ssid_item) || !ssid_item->valuestring[0]) {
        cJSON_Delete(body);
        return send_error(req, 400, "ssid is required");
    }

    const char *ssid     = ssid_item->valuestring;
    const char *password = (cJSON_IsString(pass_item)) ? pass_item->valuestring : "";

    if (strlen(ssid) >= CONFIG_WIFI_SSID_MAX) {
        cJSON_Delete(body);
        return send_error(req, 400, "ssid too long");
    }
    if (strlen(password) >= CONFIG_WIFI_PASS_MAX) {
        cJSON_Delete(body);
        return send_error(req, 400, "password too long");
    }

    // Save credentials into NVS via config_mgr.
    device_config_t cfg;
    config_mgr_get_active(ctx->config, &cfg);
    strncpy(cfg.wifi_ssid,     ssid,     sizeof(cfg.wifi_ssid) - 1);
    strncpy(cfg.wifi_password, password, sizeof(cfg.wifi_password) - 1);
    config_mgr_set_staged(ctx->config, &cfg);
    config_mgr_apply(ctx->config);
    esp_err_t err = config_mgr_commit(ctx->config);
    cJSON_Delete(body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_mgr_commit failed: %s", esp_err_to_name(err));
        return send_error(req, 500, "failed to save credentials");
    }

    ESP_LOGI(TAG, "WiFi credentials saved for SSID: %s — rebooting", ssid);

    // Respond before rebooting.
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "connecting");
    esp_err_t ret = send_json(req, resp, 200);

    // Reboot in background after the response lands.
    xTaskCreate(reboot_task, "provision_reboot", 1024, NULL, 3, NULL);

    return ret;
}

// GET /provision/status — polled by page JS; shows current STA link state.
esp_err_t handle_provision_status(httpd_req_t *req)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(obj,   "connected", false);  // always false in AP mode
    cJSON_AddStringToObject(obj, "ip",        "");
    return send_json(req, obj, 200);
}
