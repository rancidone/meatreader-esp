import AsyncStorage from '@react-native-async-storage/async-storage';

export interface SavedDevice {
  ip: string;
  label?: string;
  lastSeenMs?: number;
}

const STORAGE_KEY = '@meatreader/devices';
const ACTIVE_KEY = '@meatreader/active_ip';

export async function loadDevices(): Promise<SavedDevice[]> {
  const raw = await AsyncStorage.getItem(STORAGE_KEY);
  return raw ? (JSON.parse(raw) as SavedDevice[]) : [];
}

export async function saveDevices(devices: SavedDevice[]): Promise<void> {
  await AsyncStorage.setItem(STORAGE_KEY, JSON.stringify(devices));
}

export async function addDevice(ip: string, label?: string): Promise<SavedDevice[]> {
  const devices = await loadDevices();
  if (!devices.find((d) => d.ip === ip)) {
    const updated = [...devices, { ip, label, lastSeenMs: Date.now() }];
    await saveDevices(updated);
    return updated;
  }
  return devices;
}

export async function removeDevice(ip: string): Promise<SavedDevice[]> {
  const devices = await loadDevices();
  const updated = devices.filter((d) => d.ip !== ip);
  await saveDevices(updated);
  return updated;
}

export async function touchDevice(ip: string): Promise<void> {
  const devices = await loadDevices();
  const updated = devices.map((d) =>
    d.ip === ip ? { ...d, lastSeenMs: Date.now() } : d,
  );
  await saveDevices(updated);
}

export async function getActiveIp(): Promise<string | null> {
  return AsyncStorage.getItem(ACTIVE_KEY);
}

export async function setActiveIp(ip: string): Promise<void> {
  await AsyncStorage.setItem(ACTIVE_KEY, ip);
}

export async function clearActiveIp(): Promise<void> {
  await AsyncStorage.removeItem(ACTIVE_KEY);
}
