# Meatreader ESP32 — Implementation TODO

## Next: Native Mobile + Device UI Roadmap

### Summary

- Reposition the product around a production-grade native mobile app as the primary user interface.
- Keep the existing Svelte web UI as a device-local setup/admin console instead of the main end-user app.
- Add an on-device TFT UI track using physical buttons or encoder input.
- Keep firmware HTTP APIs as the near-term integration surface for both native mobile and local web admin flows.

### Phase 8: Product Reframe and Repository Structure

- [x] Document the new product direction in `README.md`:
  - [x] Native mobile app is the primary app surface.
  - [x] Web UI is primarily for setup, calibration, diagnostics, and fallback access.
  - [x] PWA is no longer the primary install/distribution path.
- [x] Define the repo/workspace layout for a new native app package.
- [x] Decide how shared TypeScript API/domain models will be reused between `thermometer-ui` and the native app.
- [ ] Capture the minimum supported mobile feature set for the first release:
  - [ ] Live dashboard
  - [ ] Device connection management by local IP / saved device
  - [ ] Cook profile apply/use
  - [ ] Alert visibility and control
  - [ ] Device health/status visibility

### Phase 9: Native Mobile App Foundation (React Native + Expo)

- [ ] Create a new React Native + Expo application in the repo.
- [ ] Establish the mobile app architecture:
  - [ ] API client layer for existing ESP32 HTTP endpoints
  - [ ] Live update mechanism for temperature/status refresh
  - [ ] Device persistence for remembered local devices
  - [ ] Shared domain types where practical
- [ ] Build the first mobile navigation shell.
- [ ] Implement the first-pass instrument-panel dashboard UX:
  - [ ] Large channel temperature tiles
  - [ ] Prominent connection/health state
  - [ ] Fast thumb-friendly controls
  - [ ] Simple chart/history entry point if feasible in v1
- [ ] Implement the first mobile device onboarding flow:
  - [ ] Add/connect device by IP on the local network
  - [ ] Save and switch between remembered devices
- [ ] Implement first-pass mobile actions:
  - [ ] View/apply cook profiles
  - [ ] Surface alert state and triggered conditions
- [ ] Defer advanced config, calibration, and OTA parity until after the dashboard baseline unless blocked by real usage.

### Phase 10: Web UI Rescope and Cleanup

- [ ] Fix the current `thermometer-ui` production build failure in the PWA/Workbox packaging step.
- [ ] Remove or reduce PWA-specific product work that no longer matches the roadmap.
- [ ] Keep the web UI usable as a local admin console:
  - [ ] Config
  - [ ] Calibration
  - [ ] Diagnostics
  - [ ] Firmware update
- [ ] Improve narrow-screen responsiveness enough for emergency/mobile browser use.
- [ ] Avoid major design investment in the current browser shell beyond admin usability, since native mobile is the main UX track.

### Phase 11: Firmware API Alignment for Native Clients

- [ ] Review existing HTTP endpoints for native-app ergonomics.
- [ ] Identify API gaps for:
  - [ ] Device onboarding/discovery support
  - [ ] Stable dashboard data loading
  - [ ] Alert/profile actions
  - [ ] Device metadata and health
- [ ] Add only additive API changes where possible.
- [ ] If current endpoints are too fragmented, add a consolidated dashboard-oriented read model for app clients.
- [ ] Preserve compatibility for the existing local web admin console during API additions.

### Phase 12: On-Device TFT UI Foundation

- [ ] Record the exact TFT hardware details before implementation:
  - [ ] Controller
  - [ ] Resolution
  - [ ] Bus type
  - [ ] Color depth / memory constraints
  - [ ] Input hardware (buttons / encoder)
- [ ] Add a firmware display subsystem abstraction instead of wiring display logic directly into `main.c`.
- [ ] Add an input/navigation abstraction for physical controls.
- [ ] Build the first on-device UI as a status/control surface, not a full clone of the mobile app.
- [ ] Implement the first screen set:
  - [ ] Channel temperatures and labels
  - [ ] Connectivity / provisioning state
  - [ ] Alert / cook-profile state
  - [ ] Simple menu navigation
- [ ] Keep the UI optimized for a ~2 inch TFT:
  - [ ] Large numerals
  - [ ] High contrast
  - [ ] Minimal hierarchy depth
  - [ ] No touch dependency in v1

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
