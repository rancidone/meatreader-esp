# CLAUDE.md

## Repository layout

```
firmware/          — ESP-IDF v5.5.3 C firmware (primary codebase)
thermometer-ui/    — Svelte 5 + TypeScript web UI
```

## Firmware (ESP-IDF)

Requires ESP-IDF v5.5.3. Uses the **new I2C master API** (`i2c_new_master_bus`), not the legacy `i2c_driver_install`.

```bash
cd firmware
idf.py build
idf.py flash monitor
idf.py menuconfig    # set WiFi via "Meatreader WiFi" menu
```

**Constraint:** Never commit `sdkconfig` with WiFi credentials baked in.

### Components (`firmware/components/`)

| Component | Purpose |
|-----------|---------|
| `board` | All hardware pin/circuit constants. Single source of truth — change here when hardware changes. |
| `i2c_manager` | Singleton I2C master bus (`i2c_manager_get_bus()`). |
| `ads1115` | ADS1115 ADC driver; single-shot, 128 SPS, PGA ±4.096V. |
| `ref_sensor` | DS18B20 1-Wire reference thermometer (GPIO 13). Calibration reference only. |
| `therm_math` | Pure math: ADC→resistance, Steinhart-Hart→°C, EMA. No ESP-IDF deps; host-testable. |
| `config_mgr` | 3-state config: persisted (NVS) / active / staged. Mutex-protected. |
| `sensor_mgr` | FreeRTOS task sampling all ADS1115 channels. Snapshot cache + EMA filter. |
| `calibration` | Per-channel Steinhart-Hart solver (Cramer's rule). Requires `ref_sensor`. |
| `wifi_mgr` | WPA2 STA, event-driven. HTTP server starts regardless of connection outcome. |
| `http_server` | Routes split across `routes_temps.c`, `routes_config.c`, `routes_calibration.c`, `routes_status.c`. |

### Thermistor circuit

```
VCC ── R_TOP (22.1 kΩ) ── ADC_IN ── R_SERIES (470 Ω) ── R_THERMISTOR ── GND
```

ADC measures across (R_SERIES + R_THERMISTOR). Resistance formula: `R_therm = (V_adc × R_top) / (Vref − V_adc) − R_series`. Constants in `board.h`; `therm_math` and `calibration` must use the same constants.

### HTTP API

```
GET  /temps/latest
GET  /status
GET  /device
GET  /config
PATCH /config/staged
POST /config/apply           staged → active
POST /config/commit          active → NVS
POST /config/revert-staged   staged ← active
POST /config/revert-active   active ← persisted
GET  /calibration/live?ch=N
POST /calibration/session/start?ch=N
POST /calibration/point/capture?ch=N
POST /calibration/solve?ch=N
POST /calibration/accept?ch=N
```

`/temps/history` is intentionally absent — history is maintained client-side.

## Web UI (`thermometer-ui/`)

Stack: Svelte 5 (runes), TypeScript, Vite. API types in `src/lib/api/types.ts`; fetch client in `src/lib/api/client.ts`. Stores use `.svelte.ts` with `$state`/`$derived`.

```bash
cd thermometer-ui
npm install
npm run dev      # localhost:5173, proxies API to localhost:8080
npm run build
```

To target a real device, update proxy in `vite.config.ts`.

## Testing

### Firmware — host-side unit tests (no ESP-IDF required)

```bash
cd firmware/tests && cmake -B build && cmake --build build && ctest --test-dir build
```

Tests live in `firmware/tests/therm_math/test_therm_math.c` and link only against `therm_math.c` + libm. No device needed.

### Web UI — Vitest

```bash
cd thermometer-ui
npm run test          # run once
npm run test:watch    # watch mode
npm run test:ui       # browser UI
```

Test files follow the `src/**/*.test.ts` pattern.

## Observability

Prometheus metrics are exposed at `GET /metrics` (Content-Type: `text/plain; version=0.0.4`).

Example `scrape_configs` entry:

```yaml
scrape_configs:
  - job_name: meatreader
    static_configs:
      - targets: ['<device-ip>:80']
```

Metrics: `meatreader_temperature_celsius`, `meatreader_resistance_ohms`, `meatreader_adc_raw`, `meatreader_wifi_rssi_dbm`, `meatreader_uptime_seconds`, `meatreader_channel_quality`.
