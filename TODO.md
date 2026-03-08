# Meatreader ESP32 — Implementation TODO

---

## Phase 4: Cook Profiles & Stall Detection

### Cook profiles (firmware)

- [x] Add `cook_profile_t` struct to `config_mgr.h`: `name[32]`, `num_stages`, `stage_t stages[4]` (each stage: `target_temp_c`, `alert_method`, `label[32]`)
- [x] Add `cook_profiles_t profiles[8]` to `device_config_t` (NVS blob; bump `schema_version`)
- [x] Create `GET /profiles`, `PUT /profiles/{id}`, `DELETE /profiles/{id}` routes in new `routes_profiles.c`
- [x] Register routes in `http_server.c`

### Stall detection (firmware)

- [x] Add stall detection to `alert_mgr`: track per-channel rolling window of last N readings with timestamps
- [x] If temperature delta over 20 minutes < 2°C and channel is active, set `stall_detected = true`
- [x] Emit `stall_detected` boolean per channel in snapshot JSON (consumed by SSE `/events` stream)
- [x] Fire configured alert method on stall (separate from target-temp alert); clear when temp resumes rising (delta > 1°C over 5 min)
- [x] Add `stall_alert_enabled` boolean to `channel_alert_t` in `config_mgr.h`

### Cook profiles (UI)

- [x] Create `src/lib/stores/profileStore.svelte.ts`: fetch/save profiles from `/profiles`, persist selection in `localStorage`
- [x] Create `src/views/Profiles.svelte`: profile library view — list, create, edit, delete, apply to channels
- [x] Pre-seed a read-only "starter library" in the UI (not firmware): common presets with USDA-safe temps:
  - Brisket: 165°F wrap, 203°F finish
  - Pork shoulder: 165°F wrap, 200°F finish
  - Chicken breast: 165°F
  - Medium-rare beef: 130°F
  - Medium beef: 145°F
- [x] Add profile selector dropdown to `ChannelCard` — applying a profile sets that channel's alert target
- [x] Show stall badge on `ChannelCard` when `stall_detected === true` with elapsed stall time
- [x] Multi-stage cook progress: show current stage and next target on ChannelCard when a multi-stage profile is active

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
- Alert triggered and stall detected states map to `binary_sensor` entities
- Cook profiles can be triggered via HA `select` entity (select a profile → publish to command topic)
- HA handles iOS/Android push and voice assistant queries ("Hey Google, what's the brisket at?") once entities exist
- Reuses the `esp-mqtt` connection already used for alerts; add `ha_discovery_enabled` and `mqtt_broker_url` to `device_config_t`

---

## Notes

- **NVS schema evolution**: `schema_version` in `device_config_t` handles struct changes — mismatch falls back to defaults and erases stale NVS key. Bump it whenever `device_config_t` layout changes.
- **Alert push**: ntfy.sh webhook works over plain HTTP (no TLS). MQTT to local Mosquitto is the more resilient local-network option and doubles as the HA integration transport.
- **SSE**: `GET /events` streams snapshot JSON via chunked send; bit 0 of `sensor_mgr`'s event group signals each new snapshot. Service worker (Phase 5) reconnects to this same endpoint.
- **Prometheus over OTel**: No stable OTel C SDK for ESP-IDF; Prometheus text format is a trivial `snprintf` loop.
- **Hardware v2**: PCB design in progress. `board.h` is the single source of truth — update constants there after hardware changes and recalibrate.
- **Remote/cloud access**: Deferred indefinitely. Device is intentionally LAN-first.
