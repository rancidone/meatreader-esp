// Shared TypeScript types derived directly from the firmware JSON shapes.
// Keep in sync with:
//   src/sensors/model.toit  — Snapshot, ChannelReading
//   src/config/model.toit   — DeviceConfig, ChannelConfig, SteinhartHartCoeffs
//   src/calibration/model.toit — CalibrationSession, CalibrationPoint
//   src/http/routes.toit    — response envelopes

// ── Sensor ───────────────────────────────────────────────────────────────────

export type Quality = 'ok' | 'stale' | 'disabled' | 'error';

export interface ChannelReading {
  id: number;
  temperature_c: number;
  quality: Quality;
  // Present in /temps/latest (full map); absent in /temps/history (lean map).
  raw_adc?: number;
  resistance_ohms?: number;
}

export interface Snapshot {
  timestamp: number;       // ms since Unix epoch
  channels: ChannelReading[];
  // Present in /temps/latest (full map); absent in /temps/history (lean map).
  health_flags?: string[];
}

export interface HistoryResponse {
  seconds: number;
  count: number;
  snapshots: Snapshot[];
}

// ── Status / Device ──────────────────────────────────────────────────────────

export interface StatusResponse {
  status: string;
  healthy: boolean;
  health_flags: string[];
  uptime_ms: number;
  firmware: string;
}

export interface DeviceResponse {
  platform: string;
  firmware: string;
  channels: number;
}

// ── Config ───────────────────────────────────────────────────────────────────

export interface SteinhartHartCoeffs {
  a: number;
  b: number;
  c: number;
}

export interface ChannelConfig {
  enabled: boolean;
  adc_channel: number;
  divider_resistor_ohms: number;
  steinhart_hart: SteinhartHartCoeffs;
}

export interface DeviceConfig {
  wifi_ssid: string;
  wifi_password: string;
  sample_rate_hz: number;
  publish_rate_hz: number;
  ema_alpha: number;
  spike_reject_delta_c: number;
  channels: ChannelConfig[];
}

// Partial patch body for PATCH /config/staged.
export type DeviceConfigPatch = Partial<Omit<DeviceConfig, 'channels'>> & {
  channels?: ChannelConfig[];
};

export interface ConfigStatus {
  persisted: DeviceConfig;
  active: DeviceConfig;
  staged: DeviceConfig;
}

// ── Calibration ──────────────────────────────────────────────────────────────

export interface CalibrationLiveResponse {
  channel: number;
  reference_temp_c: number | null;   // null if DS18B20 not wired/available
  thermistor_temp_c: number | null;  // null if no sensor snapshot yet
  raw_adc: number;
  quality: Quality;
}

export interface CalibrationPoint {
  reference_temp_c: number;
  raw_adc: number;
}

export interface CalibrationSession {
  channel: number;
  points: CalibrationPoint[];
}

// ── Error ────────────────────────────────────────────────────────────────────

export class ApiError extends Error {
  constructor(
    public readonly httpStatus: number,
    message: string,
  ) {
    super(message);
    this.name = 'ApiError';
  }
}
