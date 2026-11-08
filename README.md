# Meatreader ESP32

ESP32-based wireless meat thermometer. Firmware reads up to 4 thermistor channels via ADS1115 ADC, applies Steinhart-Hart calibration, and exposes an HTTP API consumed by a web UI served from the device.

## Product Direction

This is a personal project in development, not a validated commercial thermometer. V0 demonstrated probe-to-phone readings. The later V2 board is assembled and has needed wiring repairs; the repository does not establish post-repair four-probe accuracy, Wi-Fi stability, or battery runtime. Implemented software features should not be read as hardware qualification results. The on-device display remains unfinished in MeatReader.

The [build story](https://mattmadeitwork.com/projects/meatreader/) documents the revisions and repairs.

**Web UI** (`thermometer-ui/`) is the primary interface — live dashboard, cook profiles, alerts, calibration, config, diagnostics, and firmware updates. Accessible from any browser on the local network, including mobile browsers.

**On-device TFT UI** (planned) will provide a heads-up status display with physical button navigation — no phone required during a cook.

The ESP32 firmware HTTP API is the integration surface. LAN-first; no cloud dependency.

## Repository Layout

```
firmware/          — ESP-IDF v5.5.3 C firmware (ESP32)
thermometer-ui/    — Svelte 5 + TypeScript web UI (served from device SPIFFS)
pcb/               — later board design and printable enclosure files
```

## HTTP API

All endpoints return JSON. The device runs an HTTP server on port 80.

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/dashboard` | **Consolidated** — snapshot + status + alerts in one call. `snapshot` is `null` until first sensor reading; never returns 503. Preferred for mobile clients. |
| `GET` | `/temps/latest` | Latest sensor snapshot (all channels). |
| `GET` | `/status` | Device health, uptime, firmware version, WiFi RSSI. |
| `GET` | `/device` | Static device metadata (platform, channel count, firmware). |
| `GET` | `/config` | Full config object: persisted / active / staged. |
| `PATCH` | `/config/staged` | Partial patch to staged config. |
| `POST` | `/config/apply` | Promote staged → active. |
| `POST` | `/config/commit` | Persist active → NVS. |
| `POST` | `/config/revert-staged` | Revert staged ← active. |
| `POST` | `/config/revert-active` | Revert active ← persisted (NVS). |
| `GET` | `/alerts` | Per-channel alert config + live `triggered` state. |
| `PATCH` | `/alerts/staged` | Update staged alert config. |
| `GET` | `/profiles` | All 8 cook profile slots (empty slots have `name=""`). |
| `PUT` | `/profiles/{id}` | Write a cook profile slot. |
| `DELETE` | `/profiles/{id}` | Clear a cook profile slot. |
| `GET` | `/calibration/live` | Live calibration reading for a channel (`?ch=N`). |
| `POST` | `/calibration/session/start` | Begin calibration session. |
| `POST` | `/calibration/point/capture` | Capture a calibration point. |
| `POST` | `/calibration/solve` | Solve Steinhart-Hart coefficients from captured points. |
| `POST` | `/calibration/accept` | Write solved coefficients to staged config. |
| `GET` | `/metrics` | Prometheus text metrics (temperature, resistance, ADC, RSSI, uptime). |
| `GET` | `/events` | Server-Sent Events stream — emits snapshot JSON on each sensor tick. |
| `POST` | `/ota` | Upload firmware binary for OTA update. |
| `POST` | `/ota/rollback` | Roll back to the previous OTA partition. |

TypeScript API types are in `thermometer-ui/src/lib/api/types.ts`. The browser client is in `thermometer-ui/src/lib/api/client.ts`, with the live SSE transport in `thermometer-ui/src/lib/api/live.ts`. There is no separate native mobile application in this repository.

The [archived hardware repository](https://github.com/rancidone/meatreader-pcb) preserves earlier hardware work. Check the physical board revision against `pcb/` and the constants in `firmware/components/board/` before building or flashing.

## Production Build & Flash

Flash the full self-contained firmware (ESP32 + web UI served from SPIFFS):

### Prerequisites

1. Install [ESP-IDF v5.5.3](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32/get-started/)
2. Install Node.js 22.12+ (or a compatible newer LTS release).

### One-command flash

```bash
source $IDF_PATH/export.sh   # activate ESP-IDF toolchain
./build-all.sh               # builds UI, then builds + flashes firmware + SPIFFS image
```

### Manual steps

```bash
# 1. Build the web UI
cd thermometer-ui && npm install && npm run build && cd ..

# 2. Build firmware (SPIFFS image is auto-generated from thermometer-ui/dist/)
cd firmware && idf.py build

# 3. Flash everything (firmware + SPIFFS image)
idf.py flash
```

### Development (no flashing needed)

```bash
cd thermometer-ui && npm ci && npm run dev
```

Before starting, set the proxy targets in `thermometer-ui/vite.config.ts` to your device's LAN address. No device is needed to inspect the interface shell, but live temperatures and calibration require connected hardware; a disconnected dashboard is not a measurement demonstration.

## Checks without hardware

```sh
cd thermometer-ui
npm ci
npm test
npm run build
```

Host-side thermistor math tests can be run separately:

```sh
cmake -S firmware/tests -B /tmp/meatreader-host-tests
cmake --build /tmp/meatreader-host-tests
ctest --test-dir /tmp/meatreader-host-tests --output-on-failure
```

These checks exercise software, not probe calibration, repaired boards, enclosure fit, or runtime. Record those results on the actual hardware before describing V2 as validated.
