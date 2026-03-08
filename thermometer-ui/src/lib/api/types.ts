// Shared TypeScript types derived directly from the firmware JSON shapes.
// Keep in sync with firmware/components/http_server/http_util.h.

// ── Sensor ───────────────────────────────────────────────────────────────────

export type Quality = 'ok' | 'stale' | 'disabled' | 'error';

export interface ChannelReading {
  id: number;
  temperature_c: number;
  quality: Quality;
  // Present in /temps/latest (full map); absent in /temps/history (lean map).
  raw_adc?: number;
  resistance_ohms?: number;
  alert_triggered?: boolean;
  stall_detected?: boolean;
  stall_at?: number;  // ms since Unix epoch when stall was first detected
}

export interface Snapshot {
  timestamp: number;       // ms since Unix epoch
  channels: ChannelReading[];
  // Present in /temps/latest (full map); absent in /temps/history (lean map).
  health_flags?: string[];
}

// ── Status / Device ──────────────────────────────────────────────────────────

export interface StatusResponse {
  status: string;
  healthy: boolean;
  health_flags: string[];
  uptime_ms: number;
  firmware: string;
  wifi_rssi_dbm?: number;
  wifi_provisioned?: boolean;
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
  label?: string;
  steinhart_hart: SteinhartHartCoeffs;
}

export type AlertMethod = 'none' | 'webhook' | 'mqtt';

export interface AlertConfig {
  enabled: boolean;
  target_temp_c: number;
  method: AlertMethod;
  webhook_url: string;
}

export interface DeviceConfig {
  wifi_ssid: string;
  wifi_password: string;
  sample_rate_hz: number;
  ema_alpha: number;
  spike_reject_delta_c: number;
  channels: ChannelConfig[];
  alerts?: AlertConfig[];
}

// Partial patch body for PATCH /config/staged.
export type DeviceConfigPatch = Partial<Omit<DeviceConfig, 'channels' | 'alerts'>> & {
  channels?: ChannelConfig[];
  alerts?: AlertConfig[];
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

// ── Cook Profiles ─────────────────────────────────────────────────────────────

export interface Stage {
  label: string;
  target_temp_c: number;
  alert_method: string;
}

export interface CookProfile {
  name: string;
  num_stages: number;
  stages: Stage[];
}

// GET /profiles returns 8 slots; name="" means empty slot.
export type ProfilesResponse = CookProfile[];

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
