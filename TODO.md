# Meatreader ESP32 — Implementation TODO

Generated from planning session 2026-03-08.

---

## Phase 1: Temperature Alerts

### Firmware

- [x] Add `alert_method_t` enum to `config_mgr.h`: `ALERT_METHOD_NONE`, `ALERT_METHOD_WEBHOOK`, `ALERT_METHOD_MQTT`
- [x] Add `channel_alert_t` struct to `config_mgr.h`: `enabled`, `target_temp_c`, `method`, `webhook_url[128]`, `triggered`
- [x] Add `channel_alert_t alerts[CONFIG_NUM_CHANNELS]` to `device_config_t`
- [x] Add `uint32_t schema_version` to `device_config_t`; handle version mismatch in `config_mgr_init` (fall back to defaults, erase stale NVS key)
- [x] Create `firmware/components/alert_mgr/` component:
  - `include/alert_mgr.h`: `alert_mgr_init`, `alert_mgr_check_snapshot`, `alert_mgr_deinit`
  - `alert_mgr.c`: per-channel check; fire on `temp >= target && !triggered`; hysteresis: clear `triggered` when temp drops 5°C below target; dispatch via webhook (`esp_http_client`) or MQTT (`esp-mqtt`) in a short-lived spawned task
- [x] Wire `alert_mgr_init` in `main.c` after `sensor_mgr_start`
- [x] Call `alert_mgr_check_snapshot` after each sensor snapshot update
- [x] Add `alert_mgr_t *alert_mgr` to `http_app_ctx_t` in `http_server.h`
- [x] Create `routes_alerts.c`: `GET /alerts`, `PATCH /alerts/staged`
- [x] Extend `apply_json_patch` in `routes_config.c` to handle `alerts` array
- [x] Emit `alert_triggered` boolean per channel in `channel_reading_to_json` (from `http_util.h`)

### UI

- [x] Add `alert_triggered?: boolean` to `ChannelReading` in `types.ts`
- [x] Add `AlertConfig` type to `types.ts`: `enabled`, `target_temp_c`, `method`, `webhook_url`
- [x] Add "Alerts" section to `Config.svelte`: per-channel enabled toggle, target temp input (respects unit pref), method dropdown, conditional webhook URL input
- [x] Show alert active/triggered badge on `ChannelCard` when `alert_triggered === true`

---

## Phase 2: UI Polish

### Remove raw technical fields from dashboard

- [x] Remove `<dl class="meta">` block (raw ADC, resistance) from `ChannelCard.svelte`
- [x] Add "Raw readings" table to `Diagnostics.svelte`: poll `/temps/latest`, show `raw_adc`, `resistance_ohms`, `quality` per channel

### Channel labels

- [x] Add `char label[32]` to `channel_config_t` in `config_mgr.h`; default to `"Channel N"` if empty
- [x] Emit `label` in `channel_config_to_json`; accept in `apply_json_patch`
- [x] Show label in `ChannelCard` header instead of hardcoded `"Channel {id}"`
- [x] Add label text input to Config.svelte channel fieldsets

### Cook timer (pure UI)

- [x] Add `cookStartTime: Record<number, number | null>` to a new `cookStore.svelte.ts`
- [x] Show "Start cook" button on `ChannelCard` when channel has a valid reading
- [x] Display elapsed time HH:MM:SS as `$derived` from `Date.now()` via `setInterval`

### Predicted done time (pure UI)

- [x] Create `src/lib/stores/predictions.svelte.ts`:
  - `$derived` from `tempsStore.history` + alert target temps
  - Per-channel: least-squares linear regression on last 20 `(timestamp_s, temperature_c)` pairs
  - Return `{ channel: number, eta_ms: number | null }[]`; null if slope ≤ 0.01°C/s or < 10 data points or no target set
- [x] Display "Done in ~HH:MM" on `ChannelCard` when `eta_ms` is not null and within 24 hours

---

## Phase 3: Server-Sent Events + Live Predictions

### Firmware — SSE endpoint

- [x] Add `EventGroupHandle_t snapshot_event_group` to `struct sensor_mgr`
- [x] Expose via `sensor_mgr_get_event_group()` in `sensor_mgr.h`
- [x] Call `xEventGroupSetBits` after each snapshot update in the sensor task
- [x] Create `firmware/components/http_server/routes_events.c`:
  - `GET /events` handler
  - Set SSE headers: `Content-Type: text/event-stream`, `Cache-Control: no-cache`, `Connection: keep-alive`, `Access-Control-Allow-Origin: *`
  - Send `retry: 3000\n\n`
  - Loop: `xEventGroupWaitBits` → serialize snapshot → `httpd_resp_send_chunk("data: {json}\n\n")` → exit on send error
- [x] Register `/events` in `s_uris[]`; bump `max_uri_handlers` headroom accordingly
- [x] Add `/events` to Vite proxy in `vite.config.ts`

### UI — wire SSE

- [x] Rewrite `src/lib/api/live.ts` to use `EventSource('/events')` instead of polling
- [x] Subscribe in `tempsStore.start()` via `temps.subscribe(...)` instead of `setInterval`
- [x] Keep `deviceStore` on polling (`/status` every 15s — low frequency, no benefit from SSE)
- [x] Ensure `lastUpdated` and `error` states still update correctly via SSE callback

---

## Phase 4: Cook Profiles & Stall Detection

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

## Phase 5: Progressive Web App + Web Push Notifications

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

## Phase 6: OTA Firmware Updates

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
- Requires `esp-mqtt` component (already planned for Phase 1 alerts) — HA integration reuses the same MQTT connection with additional topics
- Add `ha_discovery_enabled` and `mqtt_broker_url` to `device_config_t`

---

## Notes

- **WiFi bootstrap**: SoftAP approach chosen — no companion app, works on every OS, DNS hijack triggers captive portal popup automatically. All provisioning HTML is a self-contained C string literal; no filesystem needed.
- **NVS schema evolution**: `schema_version` field added in Phase 1 handles future struct changes gracefully — mismatch falls back to defaults.
- **Alert push**: ntfy.sh webhook works over plain HTTP (no TLS). MQTT to local Mosquitto is the more resilient local-network option and doubles as the HA integration transport.
- **SSE vs WebSockets**: SSE chosen for simplicity — `esp_http_server` chunked sends, unidirectional (all we need), auto-reconnect in browser.
- **Prometheus over OTel**: No stable OTel C SDK for ESP-IDF; Prometheus text format is a trivial `snprintf` loop and the user already has Grafana+Prometheus.
- **Hardware v2**: PCB design already in progress. `board.h` abstraction is the intended seam — update constants there after hardware changes and recalibrate.
- **Remote/cloud access**: Deferred indefinitely. Tailscale or a cloud relay are the natural paths when needed, but the device is intentionally LAN-first.
