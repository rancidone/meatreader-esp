import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { VitePWA } from 'vite-plugin-pwa';

export default defineConfig({
  base: '/',
  plugins: [
    svelte(),
    VitePWA({
      registerType: 'autoUpdate',
      srcDir: 'src',
      filename: 'sw.ts',
      strategies: 'injectManifest',
      injectManifest: {
        // This SW does not use precaching, so disable Workbox manifest injection.
        // vite-plugin-pwa skips the workbox injectManifest step when injectionPoint
        // is undefined, avoiding the "no injection point found" build error.
        injectionPoint: undefined,
      },
      manifest: {
        name: 'Meatreader',
        short_name: 'Meatreader',
        description: 'ESP32 BBQ thermometer monitor',
        theme_color: '#b45309',
        background_color: '#1c1917',
        display: 'standalone',
        start_url: '/',
        icons: [
          { src: '/icons/icon-192.png', sizes: '192x192', type: 'image/png' },
          { src: '/icons/icon-512.png', sizes: '512x512', type: 'image/png', purpose: 'any maskable' },
        ],
      },
    }),
  ],
  test: {
    environment: 'jsdom',
    globals: true,
    include: ['src/**/*.test.ts'],
  },
  server: {
    port: 5173,
    // Proxy API calls to the device during development.
    // Adjust target to your device's IP or leave as localhost
    // if running the firmware locally via jag.
    proxy: {
      '/temps':       { target: 'http://192.168.10.121', changeOrigin: true },
      '/config':      { target: 'http://192.168.10.121', changeOrigin: true },
      '/calibration': { target: 'http://192.168.10.121', changeOrigin: true },
      '/status':      { target: 'http://192.168.10.121', changeOrigin: true },
      '/device':      { target: 'http://192.168.10.121', changeOrigin: true },
      '/metrics':     { target: 'http://192.168.10.121', changeOrigin: true },
      '/events':      { target: 'http://192.168.10.121', changeOrigin: true },
      '/profiles':    { target: 'http://192.168.10.121', changeOrigin: true },
      '/push':        { target: 'http://192.168.10.121', changeOrigin: true },
    },
  },
});
