// Temperature store — polls /temps/latest every second.
// History is accumulated client-side from each latest poll (oldest-first,
// capped at ui.historySize entries). /temps/history is never called — building
// batches of Maps on the ESP32 causes OOM at 30+ entries.
//
// Usage:
//   tempsStore.start()   ← call once when the dashboard mounts
//   tempsStore.stop()    ← call when the dashboard unmounts
//   tempsStore.latest    ← most recent Snapshot | null
//   tempsStore.history   ← Snapshot[] oldest-first (last ~60 s)
//   tempsStore.error     ← last fetch error message, or null

import { temps as tempsApi, ApiError } from '../api/live.ts';
import type { Snapshot } from '../api/live.ts';
import { ui } from './ui.svelte.ts';

class TempsStore {
  latest      = $state<Snapshot | null>(null);
  history     = $state<Snapshot[]>([]);
  lastUpdated = $state<number | null>(null);   // Date.now() ms
  error       = $state<string | null>(null);

  #timer: ReturnType<typeof setInterval> | null = null;

  start(latestMs = 1000): void {
    void this.#pollLatest();
    this.#timer = setInterval(() => void this.#pollLatest(), latestMs);
  }

  stop(): void {
    if (this.#timer !== null) { clearInterval(this.#timer); this.#timer = null; }
  }

  clearHistory(): void {
    this.history = [];
  }

  async #pollLatest(): Promise<void> {
    try {
      const snap       = await tempsApi.latest();
      this.latest      = snap;
      this.lastUpdated = Date.now();
      this.error       = null;
      // Accumulate client-side ring buffer (oldest-first for sparkline).
      const next = [...this.history, snap];
      const cap  = ui.historySize;
      this.history = next.length > cap ? next.slice(next.length - cap) : next;
    } catch (e) {
      // 503 means the sensor loop hasn't produced a reading yet — not an error worth showing.
      if (e instanceof ApiError && e.httpStatus === 503) return;
      this.error = e instanceof Error ? e.message : String(e);
    }
  }
}

export const tempsStore = new TempsStore();
