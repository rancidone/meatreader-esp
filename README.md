# Meatreader ESP32

ESP32-based wireless meat thermometer. Firmware reads up to 4 thermistor channels via ADS1115 ADC, applies Steinhart-Hart calibration, and exposes an HTTP API consumed by a native mobile app and a local web admin console.

## Product Direction

**Native mobile app** (React Native + Expo) is the primary user interface — live dashboard, cook profiles, alerts, and device management from your phone.

**Web UI** (`thermometer-ui/`) is a device-local admin console for setup, calibration, diagnostics, and fallback browser access. It is not the primary end-user experience.

**PWA install** is no longer the primary distribution path. The native mobile app replaces it as the recommended way to use Meatreader day-to-day.

**On-device TFT UI** (in progress) provides a heads-up status display with physical button navigation — no phone required during a cook.

The ESP32 firmware HTTP API is the integration surface for all clients. It remains LAN-first; no cloud dependency.

## Production Build & Flash

Flash the full self-contained firmware (ESP32 + web UI served from SPIFFS):

### Prerequisites

1. Install [ESP-IDF v5.5.3](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32/get-started/)
2. Install Node.js 18+

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
cd thermometer-ui && npm run dev   # hot-reload UI proxied to device at localhost:8080
```
