// Temperature store — receives snapshots via SSE (/events) instead of polling.
// History is accumulated client-side from each push (oldest-first, capped at
// ui.historySize entries). /temps/history is never called — building batches
// of Maps on the ESP32 causes OOM at 30+ entries.
//
// Usage:
//   tempsStore.start()   ← call once when the dashboard mounts
//   tempsStore.stop()    ← call when the dashboard unmounts
//   tempsStore.latest    ← most recent Snapshot | null
//   tempsStore.history   ← Snapshot[] oldest-first (last ~60 s)
//   tempsStore.error     ← last error message, or null

import { connectSSE, temps as tempsApi } from '../api/live.ts';
import type { Snapshot, SSEHandle } from '../api/live.ts';
import { ui } from './ui.svelte.ts';

class TempsStore {
  latest      = $state<Snapshot | null>(null);
  history     = $state<Snapshot[]>([]);
  lastUpdated = $state<number | null>(null);   // Date.now() ms
  error       = $state<string | null>(null);

  #handle: SSEHandle | null = null;

  start(): void {
    if (this.#handle !== null) return;   // already running

    // Seed history from firmware ring buffer (1-min points, up to 6 h).
    tempsApi.history().then(pts => {
      if (this.history.length > 0) return;  // SSE already provided data
      this.history = pts.map(pt => ({
        timestamp: pt.t * 1000,
        channels: pt.ch.map((temp_c, id) =>
          temp_c !== null
            ? { id, temperature_c: temp_c, quality: 'ok' as const }
            : { id, temperature_c: 0, quality: 'disabled' as const }
        ),
      }));
    }).catch(() => { /* non-fatal — SSE will fill in live data */ });

    this.#handle = connectSSE({
      onSnapshot: (snap) => {
        this.latest      = snap;
        this.lastUpdated = Date.now();
        this.error       = null;
        // Accumulate client-side ring buffer (oldest-first for sparkline).
        const next = [...this.history, snap];
        const cap  = ui.historySize;
        this.history = next.length > cap ? next.slice(next.length - cap) : next;
      },
      onError: (message) => {
        this.error = message;
      },
    });
  }

  stop(): void {
    if (this.#handle !== null) {
      this.#handle.stop();
      this.#handle = null;
    }
  }

  clearHistory(): void {
    this.history = [];
  }
}

export const tempsStore = new TempsStore();
