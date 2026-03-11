# Meatreader ESP32 — Implementation TODO

# TODO: VFS Migration to ESP-IDF 5.5.3 (Phased)

## Phase 1: Baseline Alignment
- [x] Update IDF baseline to `v5.5.3` in startup/tooling scripts.
- [x] Update docs to state `ESP-IDF v5.5.3` as required version.
- [x] Regenerate/refresh `firmware/dependencies.lock` so `idf.version = 5.5.3`.
- [x] Confirm clean configure/build uses `IDF_VER="v5.5.3"`.

## Phase 2: Storage/VFS Refactor (No Behavior Change)
- [x] Extract SPIFFS mount logic from `app_main` into a storage module.
- [x] Add internal lifecycle API:
  - [x] `storage_fs_init()`
  - [x] `storage_fs_deinit()`
- [x] Keep existing mount settings unchanged (`/spiffs`, `storage`, `max_files`).
- [x] Continue using public SPIFFS API (`esp_vfs_spiffs_register`) as the supported current-path integration.

## Phase 3: Reliability and Visibility
- [x] Add mount outcome logging with explicit error classification.
- [x] Log SPIFFS usage/capacity on successful mount (`esp_spiffs_info`).
- [x] Add mounted-state check before static route file serving.
- [x] Preserve current fallback behavior when SPIFFS is unavailable.

## Phase 4: Validation and Acceptance
- [x] Run `idf.py fullclean build` on 5.5.3 and require success.
- [x] Run existing host/unit tests in `firmware/tests` and require pass.
- [x] Verify logs show mount success/failure clearly.
- [x] Verify static assets still resolve from `/spiffs` and APIs remain functional if mount fails.

## Done Criteria
- [x] Project builds on ESP-IDF 5.5.3.
- [x] No external HTTP API changes.
- [x] No partition-table changes.
- [x] Runtime behavior unchanged except clearer storage diagnostics.

---

## Done

- **Phase 4**: Cook profiles (`cook_profile_t`, `/profiles` CRUD, profile store/UI, stall detection firmware + UI badge)
- **Phase 5**: PWA shell, service worker SSE keepalive, Web Push (VAPID), install/offline UI
- **Phase 6**: OTA firmware update (`/ota`, `/ota/rollback`, partition table, rollback on failed boot, Firmware.svelte UI)
- **Phase 7**: SPIFFS static file serving — `routes_static.c` catch-all, SPA fallback, SPIFFS mount in `main.c`, `spiffs_create_partition_image` CMake target, `build-all.sh`, README

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
