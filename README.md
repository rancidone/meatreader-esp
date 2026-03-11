# Meatreader ESP32

ESP32-based wireless meat thermometer. Firmware reads up to 4 thermistor channels via ADS1115 ADC, applies Steinhart-Hart calibration, and serves a Svelte web UI from SPIFFS.

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
