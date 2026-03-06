// Calibration store — manages in-progress calibration session state.
//
// Workflow:
//   startSession(ch)  → creates/resets session on device
//   capturePoint(ch)  → triggers DS18B20+ADS1115 read (stub on firmware until Pass 7)
//   solve(ch)         → computes Steinhart-Hart coefficients from captured points
//   accept(ch)        → writes solved coefficients to staged config
//   reset()           → clears local state

import { calibration as calApi } from '../api/live.ts';
import type { CalibrationSession, SteinhartHartCoeffs } from '../api/live.ts';

class CalibrationStore {
  session            = $state<CalibrationSession | null>(null);
  pendingCoefficients = $state<SteinhartHartCoeffs | null>(null);
  loading            = $state(false);
  error              = $state<string | null>(null);

  async startSession(ch: number): Promise<void> {
    await this.#run(async () => {
      const res     = await calApi.startSession(ch);
      this.session  = res.session;
      this.pendingCoefficients = null;
    });
  }

  async capturePoint(ch: number): Promise<void> {
    await this.#run(async () => {
      const res    = await calApi.capturePoint(ch);
      this.session = res.session;
    });
  }

  async solve(ch: number): Promise<void> {
    await this.#run(async () => {
      const res = await calApi.solve(ch);
      this.pendingCoefficients = res.coefficients;
    });
  }

  async accept(ch: number): Promise<void> {
    await this.#run(async () => {
      await calApi.accept(ch);
      this.pendingCoefficients = null;
    });
  }

  reset(): void {
    this.session             = null;
    this.pendingCoefficients = null;
    this.error               = null;
  }

  async #run(action: () => Promise<void>): Promise<void> {
    this.loading = true;
    this.error   = null;
    try {
      await action();
    } catch (e) {
      this.error = e instanceof Error ? e.message : String(e);
    } finally {
      this.loading = false;
    }
  }
}

export const calibrationStore = new CalibrationStore();
