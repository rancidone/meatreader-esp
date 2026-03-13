import type { DeviceConfig, DeviceConfigPatch, DeviceResponse, ProfilesResponse, Snapshot, StatusResponse } from '@meatreader/api-types';
import { ApiError } from '@meatreader/api-types';

export class MeatreaderClient {
  private baseUrl: string;

  constructor(deviceIp: string) {
    this.baseUrl = `http://${deviceIp}`;
  }

  private async request<T>(path: string, init?: RequestInit): Promise<T> {
    const res = await fetch(`${this.baseUrl}${path}`, init);
    if (!res.ok) {
      throw new ApiError(res.status, `${init?.method ?? 'GET'} ${path} → ${res.status}`);
    }
    return res.json() as Promise<T>;
  }

  getLatest(): Promise<Snapshot> {
    return this.request('/temps/latest');
  }

  getStatus(): Promise<StatusResponse> {
    return this.request('/status');
  }

  getDevice(): Promise<DeviceResponse> {
    return this.request('/device');
  }

  getConfig(): Promise<DeviceConfig> {
    return this.request('/config');
  }

  patchConfig(patch: DeviceConfigPatch): Promise<void> {
    return this.request('/config/staged', {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(patch),
    });
  }

  getProfiles(): Promise<ProfilesResponse> {
    return this.request('/profiles');
  }
}
