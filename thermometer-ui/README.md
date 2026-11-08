# Meatreader UI

Local-first web UI for the ESP32 thermometer firmware.

## Quick start

```bash
cd thermometer-ui
npm install
npm run dev
```

Open http://localhost:5173

## Pointing at the device

API calls use the development proxy targets in `vite.config.ts`. Set all
of those targets to your device's LAN address before starting the dev server, e.g.:

```ts
proxy: {
  '/temps':       { target: 'http://192.168.1.42', changeOrigin: true },
  '/config':      { target: 'http://192.168.1.42', changeOrigin: true },
  '/calibration': { target: 'http://192.168.1.42', changeOrigin: true },
  '/status':      { target: 'http://192.168.1.42', changeOrigin: true },
  '/device':      { target: 'http://192.168.1.42', changeOrigin: true },
},
```

## Build for static embedding

```bash
npm run build
```

Output lands in `dist/`. The firmware build packages these static assets into
SPIFFS for serving from the ESP32. See the root README for build and flash steps.

Without a connected device, the dashboard shows its disconnected state, not
live probe readings. An interface preview is not hardware validation.

## Structure

```
src/
  main.ts           — entry point
  app.css           — global design tokens and base styles
  App.svelte        — tab shell
  views/            — one file per top-level view
  lib/
    api/            — types and fetch client (Pass 2+)
    stores/         — Svelte stores + polling (Pass 3+)
    components/     — reusable components (Pass 4+)
    utils/          — formatting helpers (Pass 4+)
```
