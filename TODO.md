# Meatreader ESP32 — Implementation TODO

## Native Mobile + Device UI Roadmap (Phases 8–11 Complete)

### Summary

- Native mobile app (React Native + Expo) is the primary user interface.
- Svelte web UI is the device-local setup/admin console.
- On-device TFT UI track deferred pending hardware selection (see Future section).
- Firmware HTTP APIs serve both native mobile and local web admin flows.

### Phase 8: Product Reframe and Repository Structure

- [x] Document the new product direction in `README.md`:
  - [x] Native mobile app is the primary app surface.
  - [x] Web UI is primarily for setup, calibration, diagnostics, and fallback access.
  - [x] PWA is no longer the primary install/distribution path.
- [x] Define the repo/workspace layout for a new native app package.
- [x] Decide how shared TypeScript API/domain models will be reused between `thermometer-ui` and the native app.
- [x] Capture the minimum supported mobile feature set for the first release:
  - [x] Live dashboard
  - [x] Device connection management by local IP / saved device
  - [x] Cook profile apply/use
  - [x] Alert visibility and control
  - [x] Device health/status visibility

### Phase 9: Native Mobile App Foundation (React Native + Expo)

- [x] Create a new React Native + Expo application in the repo.
- [x] Establish the mobile app architecture:
  - [x] API client layer for existing ESP32 HTTP endpoints
  - [x] Live update mechanism for temperature/status refresh
  - [x] Device persistence for remembered local devices
  - [x] Shared domain types where practical
- [x] Build the first mobile navigation shell.
- [x] Implement the first-pass instrument-panel dashboard UX:
  - [x] Large channel temperature tiles
  - [x] Prominent connection/health state
  - [x] Fast thumb-friendly controls
  - [x] Simple chart/history entry point if feasible in v1
- [x] Implement the first mobile device onboarding flow:
  - [x] Add/connect device by IP on the local network
  - [x] Save and switch between remembered devices
- [x] Implement first-pass mobile actions:
  - [x] View/apply cook profiles
  - [x] Surface alert state and triggered conditions
- [x] Defer advanced config, calibration, and OTA parity until after the dashboard baseline unless blocked by real usage.

### Phase 10: Web UI Rescope and Cleanup

- [x] Fix the current `thermometer-ui` production build failure in the PWA/Workbox packaging step.
- [x] Remove or reduce PWA-specific product work that no longer matches the roadmap.
- [x] Keep the web UI usable as a local admin console:
  - [x] Config
  - [x] Calibration
  - [x] Diagnostics
  - [x] Firmware update
- [x] Improve narrow-screen responsiveness enough for emergency/mobile browser use.
- [x] Avoid major design investment in the current browser shell beyond admin usability, since native mobile is the main UX track.

### Phase 11: Firmware API Alignment for Native Clients

- [x] Review existing HTTP endpoints for native-app ergonomics.
- [x] Identify API gaps for:
  - [x] Device onboarding/discovery support
  - [x] Stable dashboard data loading
  - [x] Alert/profile actions
  - [x] Device metadata and health
- [x] Add only additive API changes where possible.
- [x] If current endpoints are too fragmented, add a consolidated dashboard-oriented read model for app clients.
- [x] Preserve compatibility for the existing local web admin console during API additions.

### Testing and Acceptance

- Firmware:
  - Existing host/unit tests still pass.
  - Display/input code builds cleanly with the selected hardware configuration.
  - Device remains functional with display subsystem enabled and disabled.
- Web admin UI:
  - `npm test` passes.
  - Production build succeeds after packaging cleanup.
  - Config, calibration, diagnostics, and firmware update flows remain usable.
  - Narrow-screen browser access is validated for fallback use.
- Native mobile app:
  - App can connect to a device by local IP.
  - Live temperatures update reliably during a cook.
  - Dashboard renders clearly on iPhone-sized screens.
  - Profile and alert actions succeed against a real device.
  - iOS validation is required for the first release milestone.
  - Android smoke coverage follows once the baseline app flow is stable.

### Assumptions and Defaults

- Native mobile replaces PWA as the primary product direction.
- React Native + Expo is the selected mobile stack.
- iOS quality is the first-class target, but the app should remain cross-platform-capable.
- The existing Svelte app remains in the repo as a setup/admin console.
- The on-device UI targets a non-touch TFT with physical controls for the first iteration.
- Exact TFT hardware details still need to be captured before implementation starts.

---

## Done

- **VFS Migration (Phases 1–4)**: IDF 5.5.3 baseline, storage component extraction, mount logging/capacity, mounted-state guard, build verified
- **Phase 4**: Cook profiles (`cook_profile_t`, `/profiles` CRUD, profile store/UI, stall detection firmware + UI badge)
- **Phase 5**: PWA shell, service worker SSE keepalive, Web Push (VAPID), install/offline UI
- **Phase 6**: OTA firmware update (`/ota`, `/ota/rollback`, partition table, rollback on failed boot, Firmware.svelte UI)
- **Phase 7**: SPIFFS static file serving — `routes_static.c` catch-all, SPA fallback, SPIFFS mount in `main.c`, `spiffs_create_partition_image` CMake target, `build-all.sh`, README
- **Phase 8**: Product reframe — README updated, npm workspace layout, shared `@meatreader/api-types` package, mobile feature scope documented
- **Phase 9**: React Native + Expo mobile app — navigation shell, dashboard tiles, device onboarding, cook profiles, alerts, temperature history chart tab
- **Phase 10**: Web UI rescope — PWA build fixed (Workbox `injectionPoint`), install banner removed, narrow-screen responsive topbar
- **Phase 11**: Firmware API alignment — `GET /dashboard` consolidated endpoint (snapshot + status + alerts), `DashboardResponse` type, `getDashboard()` client method, full HTTP API table in README

---

## Future: On-Device TFT UI (Phase 12)

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
