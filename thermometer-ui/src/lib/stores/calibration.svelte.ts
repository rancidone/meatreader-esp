// Calibration store — manages in-progress calibration session state.
//
// All operations cover all channels simultaneously.
// Workflow: startSession → capturePoint (×3+) → accept → /config/commit

import { calibration as calApi } from '../api/live.ts';
import type { CalibrationSession, CalibrationChannelResult } from '../api/live.ts';

class CalibrationStore {
  sessions            = $state<CalibrationSession[]>([]);
  pendingResults      = $state<CalibrationChannelResult[]>([]);
  loading             = $state(false);
  error               = $state<string | null>(null);

  get hasSession(): boolean {
    return this.sessions.some(s => s.points.length >= 0);
  }

  get pointCount(): number {
    return this.sessions[0]?.points.length ?? 0;
  }

  async startSession(): Promise<void> {
    await this.#run(async () => {
      const res        = await calApi.startSession();
      this.sessions    = res.sessions;
      this.pendingResults = [];
    });
  }

  async capturePoint(): Promise<void> {
    await this.#run(async () => {
      const res     = await calApi.capturePoint();
      this.sessions = res.sessions;
    });
  }

  async accept(): Promise<void> {
    await this.#run(async () => {
      const res           = await calApi.accept();
      this.pendingResults = res.channels;
      if (res.status === 'ok') {
        this.sessions = [];
      }
    });
  }

  reset(): void {
    this.sessions       = [];
    this.pendingResults = [];
    this.error          = null;
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
