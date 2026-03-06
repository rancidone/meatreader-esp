// Typed API client — one function per firmware endpoint.
//
// All paths are relative (e.g. "/temps/latest") so that Vite's dev proxy
// routes them to the device. To point at a real device IP, change the proxy
// target in vite.config.ts — no changes needed here.

import {
  ApiError,
  type CalibrationLiveResponse,
  type ConfigStatus,
  type DeviceConfig,
  type DeviceConfigPatch,
  type DeviceResponse,
  type HistoryResponse,
  type Snapshot,
  type SteinhartHartCoeffs,
  type StatusResponse,
  type CalibrationSession,
} from './types.ts';

// ── Internal helpers ─────────────────────────────────────────────────────────

async function fetchJson<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(path, {
    headers: { 'Content-Type': 'application/json', ...init?.headers },
    ...init,
  });
  if (!res.ok) {
    let message = res.statusText;
    try {
      const body = await res.json() as { message?: string };
      if (body.message) message = body.message;
    } catch { /* ignore parse failure */ }
    throw new ApiError(res.status, message);
  }
  return res.json() as Promise<T>;
}

const get  = <T>(path: string) => fetchJson<T>(path);
const post = <T>(path: string, body?: unknown) =>
  fetchJson<T>(path, { method: 'POST', body: body !== undefined ? JSON.stringify(body) : undefined });
const patch = <T>(path: string, body: unknown) =>
  fetchJson<T>(path, { method: 'PATCH', body: JSON.stringify(body) });

// ── Temps ────────────────────────────────────────────────────────────────────

export const temps = {
  latest: () =>
    get<Snapshot>('/temps/latest'),

  history: (seconds = 60) =>
    get<HistoryResponse>(`/temps/history?seconds=${seconds}`),
};

// ── Status / Device ──────────────────────────────────────────────────────────

export const status = () =>
  get<StatusResponse>('/status');

export const device = () =>
  get<DeviceResponse>('/device');

// ── Config ───────────────────────────────────────────────────────────────────

export const config = {
  get: () =>
    get<ConfigStatus>('/config'),

  patchStaged: (body: DeviceConfigPatch) =>
    patch<{ status: string; staged: DeviceConfig }>('/config/staged', body),

  apply: () =>
    post<{ status: string; active: DeviceConfig }>('/config/apply'),

  commit: () =>
    post<{ status: string; message: string }>('/config/commit'),

  revertStaged: () =>
    post<{ status: string; staged: DeviceConfig }>('/config/revert-staged'),

  revertActive: () =>
    post<{ status: string; active: DeviceConfig }>('/config/revert-active'),
};

// ── Calibration ──────────────────────────────────────────────────────────────

export const calibration = {
  live: (ch: number) =>
    get<CalibrationLiveResponse>(`/calibration/live?ch=${ch}`),

  startSession: (ch: number) =>
    post<{ status: string; session: CalibrationSession }>(`/calibration/session/start?ch=${ch}`),

  capturePoint: (ch: number) =>
    post<{ status: string; session: CalibrationSession }>(`/calibration/point/capture?ch=${ch}`),

  solve: (ch: number) =>
    post<{ status: string; channel: number; coefficients: SteinhartHartCoeffs }>(`/calibration/solve?ch=${ch}`),

  accept: (ch: number) =>
    post<{ status: string; message: string }>(`/calibration/accept?ch=${ch}`),
};

// Convenience re-export for callers that want everything under one import.
export const api = { temps, status, device, config, calibration };

// Re-export types so consumers can import from one place.
export type { Snapshot, ChannelReading, HistoryResponse, StatusResponse, DeviceResponse,
              DeviceConfig, ChannelConfig, DeviceConfigPatch, ConfigStatus,
              SteinhartHartCoeffs, CalibrationLiveResponse, CalibrationSession,
              CalibrationPoint } from './types.ts';
export { ApiError } from './types.ts';
