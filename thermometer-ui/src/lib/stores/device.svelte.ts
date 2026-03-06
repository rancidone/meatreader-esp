// Device store — polls /status every 15 s; fetches /device once.
//
// `connected` is true if the last status fetch succeeded.
// `connected` goes false on fetch error, recovers automatically on next poll.

import { status as statusApi, device as deviceApi } from '../api/live.ts';
import type { StatusResponse, DeviceResponse } from '../api/live.ts';

class DeviceStore {
  statusData  = $state<StatusResponse | null>(null);
  deviceInfo  = $state<DeviceResponse | null>(null);
  connected   = $state(false);
  error       = $state<string | null>(null);

  #timer: ReturnType<typeof setInterval> | null = null;

  start(pollMs = 15_000): void {
    void this.#poll();
    void this.#fetchDevice();
    this.#timer = setInterval(() => void this.#poll(), pollMs);
  }

  stop(): void {
    if (this.#timer !== null) { clearInterval(this.#timer); this.#timer = null; }
  }

  refresh(): void {
    void this.#poll();
  }

  async #poll(): Promise<void> {
    try {
      this.statusData = await statusApi();
      this.connected  = true;
      this.error      = null;
    } catch (e) {
      this.connected = false;
      this.error     = e instanceof Error ? e.message : String(e);
    }
  }

  async #fetchDevice(): Promise<void> {
    try {
      this.deviceInfo = await deviceApi();
    } catch { /* non-critical */ }
  }
}

export const deviceStore = new DeviceStore();
