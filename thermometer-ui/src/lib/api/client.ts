// Typed API client — one function per firmware endpoint.
//
// All paths are relative (e.g. "/temps/latest") so that Vite's dev proxy
// routes them to the device. To point at a real device IP, change the proxy
// target in vite.config.ts — no changes needed here.

import {
  ApiError,
  type CalibrationLiveResponse,
  type CalibrationChannelResult,
  type DeviceConfig,
  type DeviceConfigPatch,
  type DeviceResponse,
  type OtaRollbackResponse,
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

// Shape returned by GET /temps/history — one point per minute, up to 360 (6 h).
export interface HistoryPoint { t: number; ch: (number | null)[]; }

export const temps = {
  latest: () =>
    get<Snapshot>('/temps/latest'),

  history: () =>
    get<HistoryPoint[]>('/temps/history'),
};

// ── Status / Device ──────────────────────────────────────────────────────────

export const status = () =>
  get<StatusResponse>('/status');

export const device = () =>
  get<DeviceResponse>('/device');

// ── Config ───────────────────────────────────────────────────────────────────

export const config = {
  get: () =>
    get<DeviceConfig>('/config'),

  patch: (body: DeviceConfigPatch) =>
    patch<{ status: string; config: DeviceConfig }>('/config', body),

  commit: () =>
    post<{ status: string; message: string }>('/config/commit'),
};

// ── Calibration ──────────────────────────────────────────────────────────────

export const calibration = {
  live: () =>
    get<CalibrationLiveResponse>('/calibration/live'),

  startSession: () =>
    post<{ status: string; sessions: CalibrationSession[] }>('/calibration/session/start'),

  capturePoint: () =>
    post<{ status: string; reference_temp_c: number; reference_temp_f: number; sessions: CalibrationSession[] }>('/calibration/point/capture'),

  solve: () =>
    post<{ status: string; channels: CalibrationChannelResult[] }>('/calibration/solve'),

  accept: () =>
    post<{ status: string; message: string; channels: CalibrationChannelResult[] }>('/calibration/accept'),
};

// ── OTA ──────────────────────────────────────────────────────────────────────

export const ota = {
  rollback: () =>
    post<OtaRollbackResponse>('/ota/rollback'),
};

// Convenience re-export for callers that want everything under one import.
export const api = { temps, status, device, config, calibration, ota };

// Re-export types so consumers can import from one place.
export type { Snapshot, ChannelReading, StatusResponse, DeviceResponse,
              DeviceConfig, ChannelConfig, DeviceConfigPatch,
              SteinhartHartCoeffs, CalibrationLiveResponse, CalibrationChannelLive,
              CalibrationChannelResult, CalibrationSession,
              CalibrationPoint, Stage, CookProfile, ProfilesResponse,
              OtaRollbackResponse } from './types.ts';
export { ApiError } from './types.ts';
