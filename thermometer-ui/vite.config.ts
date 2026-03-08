import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

export default defineConfig({
  plugins: [svelte()],
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
      '/temps': { target: 'http://192.168.10.121', changeOrigin: true },
      '/config': { target: 'http://192.168.10.121', changeOrigin: true },
      '/calibration': { target: 'http://192.168.10.121', changeOrigin: true },
      '/status': { target: 'http://192.168.10.121', changeOrigin: true },
      '/device': { target: 'http://192.168.10.121', changeOrigin: true },
      '/metrics': { target: 'http://192.168.10.121', changeOrigin: true },
    },
  },
});
