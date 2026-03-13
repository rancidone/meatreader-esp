# Meatreader ESP32 — Implementation TODO

---

## Done

- **VFS Migration (Phases 1–4)**: IDF 5.5.3 baseline, storage component extraction, mount logging/capacity, mounted-state guard, build verified
- **Phase 4**: Cook profiles (`cook_profile_t`, `/profiles` CRUD, profile store/UI, stall detection firmware + UI badge)
- **Phase 5**: PWA shell, service worker SSE keepalive, Web Push (VAPID), install/offline UI
- **Phase 6**: OTA firmware update (`/ota`, `/ota/rollback`, partition table, rollback on failed boot, Firmware.svelte UI)
- **Phase 7**: SPIFFS static file serving — `routes_static.c` catch-all, SPA fallback, SPIFFS mount in `main.c`, `spiffs_create_partition_image` CMake target, `build-all.sh`, README
- **Phase 10**: Web UI rescope — PWA build fixed (Workbox `injectionPoint`), install banner removed, narrow-screen responsive topbar
- **Phase 11**: Firmware API alignment — `GET /dashboard` consolidated endpoint (snapshot + status + alerts), full HTTP API table in README

## Active

- [ ] **Bug: main task stack overflow on boot** — `device_config_t cfg` (~2 KB) is stack-allocated in `app_main` (main.c:105); main task stack is only 4096 bytes, exhausted by the boot sequence. Fix: (1) raise `CONFIG_ESP_MAIN_TASK_STACK_SIZE` to 8192 in `sdkconfig.defaults`, (2) heap-allocate `device_config_t` in `app_main` instead of stack.

---

## Future: On-Device TFT UI

*Blocked pending TFT hardware selection. Capture controller, resolution, bus type, color depth, and input hardware (buttons/encoder) before starting.*

- Add a firmware display subsystem abstraction instead of wiring display logic directly into `main.c`.
- Add an input/navigation abstraction for physical controls.
- First screen set: channel temperatures, connectivity/provisioning state, alert/profile state, simple menu navigation.
- Optimized for ~2 inch TFT: large numerals, high contrast, minimal hierarchy depth, no touch in v1.

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
