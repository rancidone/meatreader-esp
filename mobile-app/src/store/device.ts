// Persisted device registry — saved devices by IP.
// Full implementation in Phase 9 (device onboarding flow).

export interface SavedDevice {
  ip: string;
  label?: string;
  lastSeenMs?: number;
}

// In-memory state for now; will be backed by AsyncStorage in Phase 9.
let devices: SavedDevice[] = [];
let activeIp: string | null = null;

export function getSavedDevices(): SavedDevice[] {
  return devices;
}

export function getActiveIp(): string | null {
  return activeIp;
}

export function setActiveDevice(ip: string): void {
  activeIp = ip;
  if (!devices.find((d) => d.ip === ip)) {
    devices = [...devices, { ip }];
  }
}

export function removeDevice(ip: string): void {
  devices = devices.filter((d) => d.ip !== ip);
  if (activeIp === ip) activeIp = devices[0]?.ip ?? null;
}
