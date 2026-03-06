# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository layout

```
firmware/          — ESP-IDF 5.1+ C firmware (the primary codebase)
thermometer-ui/    — Svelte 5 + TypeScript web UI
toit-src/          — Abandoned Toit prototype (reference only, has known bugs)
```

## Firmware (ESP-IDF)

### Build & flash

All commands run from `firmware/`:

```bash
cd firmware
idf.py build
idf.py flash monitor          # flash + open serial monitor
idf.py menuconfig             # configure WiFi SSID/password via "Meatreader WiFi" menu
```

Requires ESP-IDF 5.1+ sourced in the shell (`source $IDF_PATH/export.sh`). The project uses the **new I2C master API** (`i2c_new_master_bus`) — not the legacy `i2c_driver_install` API.

### WiFi credentials

Set at build time via `idf.py menuconfig → Meatreader WiFi`, or at runtime via the HTTP API (`PATCH /config/staged` → `POST /config/apply` → `POST /config/commit`). Credentials baked into `sdkconfig` should not be committed.

### Component architecture

Each subdirectory under `firmware/components/` is an independent ESP-IDF component:

| Component | Purpose |
|-----------|---------|
| `board` | All hardware pin/circuit constants (`board.h`). Change here when hardware changes; always re-calibrate after. |
| `i2c_manager` | Singleton I2C master bus; call `i2c_manager_get_bus()` to get the handle. |
| `ads1115` | I2C driver for ADS1115 ADC; single-shot reads at 128 SPS, PGA ±4.096V. |
| `ds18b20` | 1-Wire DS18B20 temperature sensor driver (GPIO 13). Used as calibration reference only. |
| `therm_math` | Pure math: ADC→resistance, Steinhart-Hart→°C, EMA filter. **No ESP-IDF deps** — can be compiled and tested on host. |
| `config_mgr` | 3-state config: persisted (NVS) / active / staged. Mutex-protected. NVS blob key `"cfg"`. |
| `sensor_mgr` | FreeRTOS task sampling all ADS1115 channels. Caches latest snapshot; applies EMA filter. |
| `calibration` | Per-channel S-H least-squares solver (Cramer's rule). Requires DS18B20 for reference temperature. |
| `wifi_mgr` | WPA2 STA, event-driven; `wifi_mgr_wait_connected()` has a timeout (15 s) — HTTP server starts regardless. |
| `http_server` | `esp_http_server` with routes split across `routes_temps.c`, `routes_config.c`, `routes_calibration.c`, `routes_status.c`. |

### Thermistor circuit

```
VCC ── R_TOP (22.1 kΩ) ── ADC_IN ── R_SERIES (470 Ω) ── R_THERMISTOR ── GND
```

ADC measures voltage across (R_SERIES + R_THERMISTOR). Conversion: `R_therm = (V_adc × R_top) / (Vref − V_adc) − R_series`. Constants live in `board.h`; both `therm_math` and `calibration` must use the same constants.

### HTTP API

```
GET  /temps/latest
GET  /status
GET  /device
GET  /config
PATCH /config/staged         (partial JSON patch)
POST /config/apply           (staged → active)
POST /config/commit          (active → NVS)
POST /config/revert-staged   (staged ← active)
POST /config/revert-active   (active ← persisted)
GET  /calibration/live?ch=N
POST /calibration/session/start?ch=N
POST /calibration/point/capture?ch=N
POST /calibration/solve?ch=N
POST /calibration/accept?ch=N
```

`/temps/history` is intentionally absent — history is maintained client-side.

## Web UI (thermometer-ui)

```bash
cd thermometer-ui
npm install
npm run dev      # dev server at http://localhost:5173, proxies API to http://localhost:8080
npm run build    # output to dist/
```

To point at a real device, change the proxy targets in `vite.config.ts` to the device IP.

Stack: Svelte 5 (runes syntax), TypeScript, Vite. No test runner configured. API types live in `src/lib/api/types.ts`; the typed fetch client is `src/lib/api/client.ts`. Stores use Svelte 5 `$state`/`$derived` runes (`.svelte.ts` extension).

## Known incomplete areas

- `ds18b20.c`: implementation is a stub; 1-Wire needs to be implemented (RMT or GPIO bit-bang).
- `sensor_mgr.c`: EMA alpha is hardcoded `0.3f`; `cfg.ema_alpha` from config is not yet wired in.
- `therm_math` is host-testable: it has no ESP-IDF dependencies and can be compiled with a plain `gcc` + `libm` for unit testing.
