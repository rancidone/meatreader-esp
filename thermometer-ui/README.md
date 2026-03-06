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

By default, API calls are proxied to `http://localhost:8080` (the default
`jag` HTTP port). To point at a real device, edit `vite.config.ts` and change
the `proxy.target` values to your device's IP, e.g.:

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

Output lands in `dist/`. These static assets can be served from flash on the
ESP32 in a later pass.

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
