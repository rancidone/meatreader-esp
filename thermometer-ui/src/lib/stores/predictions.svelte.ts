// Predictions store — per-channel estimated time of completion.
//
// Uses least-squares linear regression on the last 20 history snapshots per
// channel to extrapolate when that channel will reach its target temperature.
//
// Usage:
//   predictions.targets[channelId]           ← target temp °C | null
//   predictions.setTarget(ch, tempC)         ← set target (null to clear)
//   predictions.forChannel(ch)               ← { eta_ms: number | null }

import { tempsStore } from './temps.svelte.ts';

const MAX_POINTS   = 20;
const MIN_POINTS   = 10;
const MIN_SLOPE    = 0.01;          // °C/s — below this we treat as flat
const MAX_ETA_MS   = 24 * 3600e3;  // 24 hours in ms

/** Linear regression over (x[], y[]) → { slope, intercept } */
function linearRegression(xs: number[], ys: number[]): { slope: number; intercept: number } {
  const n  = xs.length;
  let sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
  for (let i = 0; i < n; i++) {
    sumX  += xs[i];
    sumY  += ys[i];
    sumXY += xs[i] * ys[i];
    sumXX += xs[i] * xs[i];
  }
  const denom = n * sumXX - sumX * sumX;
  if (denom === 0) return { slope: 0, intercept: sumY / n };
  const slope     = (n * sumXY - sumX * sumY) / denom;
  const intercept = (sumY - slope * sumX) / n;
  return { slope, intercept };
}

export interface ChannelPrediction {
  channel: number;
  eta_ms:  number | null;
}

class PredictionsStore {
  /** Per-channel target temperature in °C; null means no target set. */
  targets = $state<Record<number, number | null>>({});

  setTarget(channelId: number, tempC: number | null): void {
    this.targets = { ...this.targets, [channelId]: tempC };
  }

  /** Derived per-channel predictions from history + targets. */
  get predictions(): ChannelPrediction[] {
    const history = tempsStore.history;
    // Collect all unique channel IDs seen in history
    const channelIds = new Set<number>();
    for (const snap of history) {
      for (const ch of snap.channels) channelIds.add(ch.id);
    }

    return Array.from(channelIds).map((channelId) => {
      const target = this.targets[channelId] ?? null;

      // No target → no prediction
      if (target === null) return { channel: channelId, eta_ms: null };

      // Collect last MAX_POINTS 'ok' readings for this channel
      const points: { ts: number; temp: number }[] = [];
      for (let i = history.length - 1; i >= 0 && points.length < MAX_POINTS; i--) {
        const snap = history[i];
        const ch   = snap.channels.find(c => c.id === channelId);
        if (ch && ch.quality === 'ok') {
          points.unshift({ ts: snap.timestamp / 1000, temp: ch.temperature_c });
        }
      }

      if (points.length < MIN_POINTS) return { channel: channelId, eta_ms: null };

      const xs = points.map(p => p.ts);
      const ys = points.map(p => p.temp);
      const { slope, intercept } = linearRegression(xs, ys);

      // Slope must be positive and meaningful
      if (slope <= MIN_SLOPE) return { channel: channelId, eta_ms: null };

      // eta_s = (target - intercept) / slope  (seconds since epoch)
      const etaSec = (target - intercept) / slope;
      const etaMs  = etaSec * 1000 - Date.now();

      if (etaMs <= 0 || etaMs > MAX_ETA_MS) return { channel: channelId, eta_ms: null };

      return { channel: channelId, eta_ms: etaMs };
    });
  }

  forChannel(channelId: number): ChannelPrediction {
    return this.predictions.find(p => p.channel === channelId)
      ?? { channel: channelId, eta_ms: null };
  }
}

export const predictions = new PredictionsStore();
