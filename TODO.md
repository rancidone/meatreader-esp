# Meatreader ESP32 — Implementation TODO

Generated from planning session 2026-03-08.

---

## Phase 1: Cleanup ✅

- [x] **CLAUDE.md**: Remove `toit-src/` from repository layout table (dir doesn't exist)
- [x] **CLAUDE.md**: Remove/correct "EMA alpha hardcoded" bullet — `sensor_mgr.c` already reads `cfg.ema_alpha` correctly
- [x] **`types.ts`**: Remove file-header comment pointing to `*.toit` source files; replace with pointer to `http_util.h`
- [x] **`types.ts`**: Remove `divider_resistor_ohms` from `ChannelConfig` (firmware never emits this)
- [x] **`types.ts`**: Remove `publish_rate_hz` from `DeviceConfig` (no such field in `device_config_t`)
- [x] **`types.ts`**: Remove `HistoryResponse` entirely (endpoint intentionally absent, type unused)
- [x] **`Config.svelte`**: Remove `localPublishRate` state, the "Publish rate" form field, its `saveToStaged()` inclusion, and comparison table row
- [x] **`client.ts`**: Remove `temps.history` export (calls non-existent endpoint); remove `HistoryResponse` re-export
- [x] **`live.ts`**: Remove any `HistoryResponse` references
- [x] **`therm_math.c`**: Replace "Coefficients from Toit prototype" comment with neutral description
- [x] **`board.h`**: Remove unused `BOARD_NUM_CHANNELS` (redundant with `CONFIG_NUM_CHANNELS` in `config_mgr.h`)

---

## Phase 2: WiFi Bootstrap (SoftAP + Captive Portal)

**Chosen approach:** SoftAP + Captive Portal. On first boot (or failed STA connection), the ESP32 starts as an access point ("Meatreader-Setup"). Any phone or laptop connects, the browser is redirected to a minimal config page, the user enters their SSID and password, and the device saves credentials to NVS and reboots into STA mode. No companion app, no cloud, works on every platform.

### Firmware

- [ ] Extend `wifi_mgr` to support a dual-mode boot sequence:
  - On init: read credentials from NVS (`wifi_mgr_has_credentials()`)
  - If no credentials: start SoftAP mode directly
  - If credentials present: attempt STA connection with existing timeout; on timeout/failure, fall back to SoftAP
- [ ] Add `wifi_mgr_start_softap(const char *ap_ssid)` — starts `WIFI_MODE_AP` with open auth (no password), SSID `"Meatreader-Setup"`
- [ ] Add `wifi_mgr_start_sta_and_reboot()` — saves new credentials to NVS, switches to STA, reboots on success; returns error on connect failure so captive portal can show a retry message
- [ ] SoftAP serves a DHCP range; DNS server needed for captive portal redirect — add `lwip` DNS hijack or use `esp_netif` DNS intercept to redirect all DNS queries to `192.168.4.1` (the AP gateway)
- [ ] In `main.c`: if `wifi_mgr_in_softap_mode()`, start HTTP server in provisioning mode (captive portal only, no sensor routes); otherwise start full server as normal

### Captive portal HTTP handler

- [ ] Add `routes_provision.c` to `http_server`:
  - `GET /` in SoftAP mode: serve a self-contained inline HTML page (no external assets, ~2KB) with:
    - SSID text input + password input (type="password") + Submit
    - Optional: `GET /provision/scan` returns nearby SSIDs as JSON for a dropdown (uses `esp_wifi_scan_start`)
  - `POST /provision/connect`: receives `{"ssid":"...","password":"..."}`, attempts `wifi_mgr_start_sta_and_reboot()`; returns `{"status":"connecting"}` on attempt or `{"error":"..."}` on validation failure
  - `GET /provision/status`: returns `{"connected":true/false,"ip":"..."}` — polled by the page JS while connecting
- [ ] `GET /generate_204` (Android), `GET /hotspot-detect.html` (iOS/macOS), `GET /ncsi.txt` (Windows): return `302 → http://192.168.4.1/` to trigger the captive portal popup on each OS
- [ ] The inline HTML page must be stored as a C string literal (`const char provision_html[]`) in `routes_provision.c` — no SPIFFS/LittleFS needed

### UI (normal mode — post-provisioning)

- [ ] Add a "WiFi" section to `Config.svelte` for credential rotation (change SSID/password after initial setup) — calls existing `PATCH /config/staged` + `POST /config/apply` + reboot
- [ ] Show a warning: "Saving new WiFi credentials will restart the device"
- [ ] Add `wifi_provisioned: boolean` to `/status` response so the UI can detect unconfigured state

---

## Phase 3: Unit Tests ✅

### Firmware — host-side therm_math (CTest, no ESP-IDF required)

- [x] Create `firmware/tests/CMakeLists.txt` — top-level CTest project
- [x] Create `firmware/tests/therm_math/CMakeLists.txt` — builds `therm_math_test` executable linking `therm_math.c` + libm
- [x] Create `firmware/tests/therm_math/test_therm_math.c` with cases:
  - `therm_math_adc_to_resistance`: known ADC → expected resistance; open-circuit sentinel; negative resistance sentinel
  - `therm_math_resistance_to_celsius`: known resistance → expected °C using `THERM_MATH_DEFAULT_COEFFS`
  - `therm_math_ema_update`: NaN seed returns first value; convergence; alpha=1.0 identity
  - `therm_math_sh_valid`: rejects zero/negative coefficients
- [x] Add "## Testing" section to CLAUDE.md: `cd firmware/tests && cmake -B build && cmake --build build && ctest --test-dir build`

### Web UI — Vitest

- [x] Add dev dependencies: `vitest`, `@vitest/ui`, `jsdom`, `@testing-library/svelte`, `@testing-library/jest-dom`
- [x] Add `test` block to `vite.config.ts`: `environment: 'jsdom'`, `globals: true`, `include: ['src/**/*.test.ts']`
- [x] Add `"test": "vitest"` and `"test:ui": "vitest --ui"` to `package.json` scripts
- [x] Create `src/lib/utils/format.test.ts`: test `formatTemp`, `toDisplayTemp`, `formatResistance`, `formatUptime`, `formatAge`
- [x] Create `src/lib/api/types.test.ts`: verify `ApiError` is throwable and `instanceof` works

---

## Phase 4: Temperature Alerts

### Firmware

- [ ] Add `alert_method_t` enum to `config_mgr.h`: `ALERT_METHOD_NONE`, `ALERT_METHOD_WEBHOOK`, `ALERT_METHOD_MQTT`
- [ ] Add `channel_alert_t` struct to `config_mgr.h`: `enabled`, `target_temp_c`, `method`, `webhook_url[128]`, `triggered`
- [ ] Add `channel_alert_t alerts[CONFIG_NUM_CHANNELS]` to `device_config_t`
- [ ] Add `uint32_t schema_version` to `device_config_t`; handle version mismatch in `config_mgr_init` (fall back to defaults, erase stale NVS key)
- [ ] Create `firmware/components/alert_mgr/` component:
  - `include/alert_mgr.h`: `alert_mgr_init`, `alert_mgr_check_snapshot`, `alert_mgr_deinit`
  - `alert_mgr.c`: per-channel check; fire on `temp >= target && !triggered`; hysteresis: clear `triggered` when temp drops 5°C below target; dispatch via webhook (`esp_http_client`) or MQTT (`esp-mqtt`) in a short-lived spawned task
- [ ] Wire `alert_mgr_init` in `main.c` after `sensor_mgr_start`
- [ ] Call `alert_mgr_check_snapshot` after each sensor snapshot update
- [ ] Add `alert_mgr_t *alert_mgr` to `http_app_ctx_t` in `http_server.h`
- [ ] Create `routes_alerts.c`: `GET /alerts`, `PATCH /alerts/staged`
- [ ] Extend `apply_json_patch` in `routes_config.c` to handle `alerts` array
- [ ] Emit `alert_triggered` boolean per channel in `channel_reading_to_json` (from `http_util.h`)

### UI

- [ ] Add `alert_triggered?: boolean` to `ChannelReading` in `types.ts`
- [ ] Add `AlertConfig` type to `types.ts`: `enabled`, `target_temp_c`, `method`, `webhook_url`
- [ ] Add "Alerts" section to `Config.svelte`: per-channel enabled toggle, target temp input (respects unit pref), method dropdown, conditional webhook URL input
- [ ] Show alert active/triggered badge on `ChannelCard` when `alert_triggered === true`

---

## Phase 5: UI Polish

### Remove raw technical fields from dashboard

- [ ] Remove `<dl class="meta">` block (raw ADC, resistance) from `ChannelCard.svelte`
- [ ] Add "Raw readings" table to `Diagnostics.svelte`: poll `/temps/latest`, show `raw_adc`, `resistance_ohms`, `quality` per channel

### Channel labels

- [ ] Add `char label[32]` to `channel_config_t` in `config_mgr.h`; default to `"Channel N"` if empty
- [ ] Emit `label` in `channel_config_to_json`; accept in `apply_json_patch`
- [ ] Show label in `ChannelCard` header instead of hardcoded `"Channel {id}"`
- [ ] Add label text input to Config.svelte channel fieldsets

### Cook timer (pure UI)

- [ ] Add `cookStartTime: Record<number, number | null>` to a new `cookStore.svelte.ts`
- [ ] Show "Start cook" button on `ChannelCard` when channel has a valid reading
- [ ] Display elapsed time HH:MM:SS as `$derived` from `Date.now()` via `setInterval`

### Predicted done time (pure UI)

- [ ] Create `src/lib/stores/predictions.svelte.ts`:
  - `$derived` from `tempsStore.history` + alert target temps
  - Per-channel: least-squares linear regression on last 20 `(timestamp_s, temperature_c)` pairs
  - Return `{ channel: number, eta_ms: number | null }[]`; null if slope ≤ 0.01°C/s or < 10 data points or no target set
- [ ] Display "Done in ~HH:MM" on `ChannelCard` when `eta_ms` is not null and within 24 hours

---

## Phase 6: Prometheus Telemetry ✅

- [x] Create `firmware/components/http_server/routes_metrics.c`:
  - `GET /metrics` handler
  - Build Prometheus text format with `snprintf` into `char buf[2048]`
  - Metrics: `meatreader_temperature_celsius{channel,label}`, `meatreader_resistance_ohms{channel}`, `meatreader_adc_raw{channel}`, `meatreader_wifi_rssi_dbm`, `meatreader_uptime_seconds`, `meatreader_channel_quality{channel}`
  - `Content-Type: text/plain; version=0.0.4; charset=utf-8`
  - RSSI via `esp_wifi_sta_get_ap_info`; uptime via `esp_timer_get_time`
- [x] Register `/metrics` in `s_uris[]` in `http_server.c`
- [x] Add `/metrics` to Vite proxy targets in `vite.config.ts`
- [x] Add "## Observability" section to CLAUDE.md with Prometheus `scrape_configs` snippet

---

## Phase 7: Hardware Interface Robustness ✅

- [x] **`sensor_mgr.c`**: Add bounds check in `sample_channel` — return early if `ch_cfg->adc_channel > 3`
- [x] **`sensor_mgr.c`**: Add `uint8_t consecutive_errors[CONFIG_NUM_CHANNELS]`; increment on I2C error, reset on success; skip channel for N ticks if `consecutive_errors[idx] > 5`
- [x] **`sensor_mgr.c`**: Register sensor task with task watchdog (`esp_task_wdt_add`); call `esp_task_wdt_reset` each successful cycle
- [x] **`routes_status.c`**: Add `wifi_rssi_dbm` to `/status` JSON response (0 if not connected)

---

## Phase 8: Server-Sent Events + Live Predictions

### Firmware — SSE endpoint

- [ ] Add `EventGroupHandle_t snapshot_event_group` to `struct sensor_mgr`
- [ ] Expose via `sensor_mgr_get_event_group()` in `sensor_mgr.h`
- [ ] Call `xEventGroupSetBits` after each snapshot update in the sensor task
- [ ] Create `firmware/components/http_server/routes_events.c`:
  - `GET /events` handler
  - Set SSE headers: `Content-Type: text/event-stream`, `Cache-Control: no-cache`, `Connection: keep-alive`, `Access-Control-Allow-Origin: *`
  - Send `retry: 3000\n\n`
  - Loop: `xEventGroupWaitBits` → serialize snapshot → `httpd_resp_send_chunk("data: {json}\n\n")` → exit on send error
- [ ] Register `/events` in `s_uris[]`; bump `max_uri_handlers` headroom accordingly
- [ ] Add `/events` to Vite proxy in `vite.config.ts`

### UI — wire SSE

- [ ] Rewrite `src/lib/api/live.ts` to use `EventSource('/events')` instead of polling
- [ ] Subscribe in `tempsStore.start()` via `temps.subscribe(...)` instead of `setInterval`
- [ ] Keep `deviceStore` on polling (`/status` every 15s — low frequency, no benefit from SSE)
- [ ] Ensure `lastUpdated` and `error` states still update correctly via SSE callback

---

## Phase 9: Cook Profiles & Stall Detection

### Cook profiles (firmware)

- [ ] Add `cook_profile_t` struct to `config_mgr.h`: `name[32]`, `num_stages`, `stage_t stages[4]` (each stage: `target_temp_c`, `alert_method`, `label[32]`)
- [ ] Add `cook_profiles_t profiles[8]` to `device_config_t` (NVS blob; bump `schema_version`)
- [ ] Create `GET /profiles`, `PUT /profiles/{id}`, `DELETE /profiles/{id}` routes in new `routes_profiles.c`
- [ ] Register routes in `http_server.c`

### Stall detection (firmware)

- [ ] Add stall detection to `alert_mgr`: track per-channel rolling window of last N readings with timestamps
- [ ] If temperature delta over 20 minutes < 2°C and channel is active, set `stall_detected = true`
- [ ] Emit `stall_detected` boolean per channel in snapshot JSON
- [ ] Fire configured alert method on stall (separate from target-temp alert); clear when temp resumes rising (delta > 1°C over 5 min)
- [ ] Add `stall_alert_enabled` boolean to `channel_alert_t`

### Cook profiles (UI)

- [ ] Create `src/lib/stores/profileStore.svelte.ts`: fetch/save profiles from `/profiles`, persist selection in `localStorage`
- [ ] Create `src/views/Profiles.svelte`: profile library view — list, create, edit, delete, apply to channels
- [ ] Pre-seed a read-only "starter library" in the UI (not firmware): common presets with USDA-safe temps:
  - Brisket: 165°F wrap, 203°F finish
  - Pork shoulder: 165°F wrap, 200°F finish
  - Chicken breast: 165°F
  - Medium-rare beef: 130°F
  - Medium beef: 145°F
- [ ] Add profile selector dropdown to `ChannelCard` — applying a profile sets that channel's alert target
- [ ] Show stall badge on `ChannelCard` when `stall_detected === true` with elapsed stall time
- [ ] Multi-stage cook progress: show current stage and next target on ChannelCard when a multi-stage profile is active

---

## Phase 10: Progressive Web App + Web Push Notifications

### PWA shell

- [ ] Add `vite-plugin-pwa` to dev dependencies
- [ ] Configure `VitePWA` plugin in `vite.config.ts`: `registerType: 'autoUpdate'`, `manifest` with name/icons/theme color
- [ ] Create `public/manifest.webmanifest`: app name "Meatreader", short name, icons at 192/512px, `display: standalone`, `start_url: /`
- [ ] Create placeholder app icons (192×192 and 512×512 PNG)
- [ ] Add `<link rel="manifest">` and `<meta name="theme-color">` to `index.html`

### Service worker — background SSE keepalive

- [ ] Create `src/sw.ts` (Workbox or hand-rolled): keep SSE `/events` connection alive when tab is backgrounded
- [ ] On SSE `snapshot` message: if a channel's temperature crosses its alert target, fire `self.registration.showNotification(...)` — works even with tab in background (as long as PWA is installed)
- [ ] Request `Notification` permission on first alert configuration in `Config.svelte`
- [ ] Show an "Install app" prompt in the UI header when `beforeinstallprompt` fires (Android/Chrome)

### Web Push (VAPID) — optional enhancement

- [ ] Generate VAPID key pair (one-time, stored in `localStorage` as the "server" key for this local device)
- [ ] Register push subscription in service worker via `pushManager.subscribe`
- [ ] Add `POST /push/notify` endpoint on ESP32 that accepts a JSON payload and forwards it to the browser's push endpoint via `esp_http_client` — requires outbound HTTPS (`esp-tls`)
- [ ] Evaluate feasibility: VAPID signing requires ECDSA P-256 — check if `mbedtls` (bundled in ESP-IDF) can handle it; if not, fall back to service-worker-only Notification API (no VAPID needed for same-device push)

### UI polish for PWA

- [ ] Add install/update banner component to `App.svelte`
- [ ] Add offline indicator when SSE connection is lost (already partially handled by error state)
- [ ] Cache `/device` and `/config` responses in service worker for offline display

---

## Phase 11: OTA Firmware Updates

### Firmware

- [ ] Verify `partitions.csv` (or default partition table) has two OTA app partitions (`ota_0`, `ota_1`) and an `otadata` partition — if using default 4MB table, switch to `CONFIG_PARTITION_TABLE_TWO_OTA` in `sdkconfig`
- [ ] Create `firmware/components/http_server/routes_ota.c`:
  - `POST /ota` handler: receives multipart or raw binary body via `httpd_req_recv` in a loop
  - Write chunks to OTA partition via `esp_ota_begin` / `esp_ota_write` / `esp_ota_end`
  - On success: `esp_ota_set_boot_partition` + `esp_restart()`
  - On failure: return error JSON; running partition is unchanged
  - Reject if uploaded image is smaller than minimum viable size (sanity check)
- [ ] Register `/ota` in `http_server.c`; increase `httpd_config_t` recv buffer for large uploads
- [ ] Add `POST /ota/rollback` route: calls `esp_ota_mark_app_invalid_rollback_and_reboot()` to revert to previous partition
- [ ] Add firmware version string to `GET /device` response: populate from `esp_app_get_description()->version` (set via `project_ver` in `CMakeLists.txt`)
- [ ] Mark app valid after successful boot + WiFi connect: call `esp_ota_mark_app_valid_cancel_rollback()` in `wifi_mgr` on successful connection (enables automatic rollback if new firmware never connects)

### UI

- [ ] Add `firmware_version` to `DeviceInfo` type in `types.ts`
- [ ] Create `src/views/Firmware.svelte`:
  - Display current firmware version from `/device`
  - File picker (`.bin`) with drag-and-drop zone
  - Upload progress bar (track `xhr.upload.onprogress` or `fetch` with `ReadableStream`)
  - "Uploading… Flashing… Rebooting…" state machine with polling `/device` until device comes back online
  - "Rollback" button (calls `POST /ota/rollback`) shown only when a previous partition exists
- [ ] Add Firmware link to nav
- [ ] Add warning modal: "This will restart the device. Active cook data will be preserved in NVS."

---

## Future: Home Assistant Integration

*Not yet prioritized — implement once HA is available for testing.*

- MQTT auto-discovery: device publishes `homeassistant/sensor/meatreader_ch{N}/config` on connect with HA discovery payload; HA creates entities automatically with no manual config
- Each channel becomes a `sensor` entity with `device_class: temperature`, `unit_of_measurement`, `state_topic`
- Alert triggered state maps to a `binary_sensor` entity
- Stall detected state maps to a `binary_sensor` entity
- Cook profiles can be triggered via HA `select` entity (select a profile → publish to command topic)
- HA handles iOS/Android push and voice assistant queries ("Hey Google, what's the brisket at?") once entities exist
- Requires `esp-mqtt` component (already planned for Phase 4 alerts) — HA integration reuses the same MQTT connection with additional topics
- Add `ha_discovery_enabled` and `mqtt_broker_url` to `device_config_t`

---

## Notes

- **WiFi bootstrap**: SoftAP approach chosen — no companion app, works on every OS, DNS hijack triggers captive portal popup automatically. All provisioning HTML is a self-contained C string literal; no filesystem needed.
- **NVS schema evolution**: `schema_version` field added in Phase 4 handles future struct changes gracefully — mismatch falls back to defaults.
- **Alert push**: ntfy.sh webhook works over plain HTTP (no TLS). MQTT to local Mosquitto is the more resilient local-network option and doubles as the HA integration transport.
- **SSE vs WebSockets**: SSE chosen for simplicity — `esp_http_server` chunked sends, unidirectional (all we need), auto-reconnect in browser.
- **Prometheus over OTel**: No stable OTel C SDK for ESP-IDF; Prometheus text format is a trivial `snprintf` loop and the user already has Grafana+Prometheus.
- **Hardware v2**: PCB design already in progress. `board.h` abstraction is the intended seam — update constants there after hardware changes and recalibrate.
- **Remote/cloud access**: Deferred indefinitely. Tailscale or a cloud relay are the natural paths when needed, but the device is intentionally LAN-first.
