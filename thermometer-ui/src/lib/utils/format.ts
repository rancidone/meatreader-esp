import type { TempUnit } from '../stores/ui.svelte.ts';

export function toDisplayTemp(c: number, unit: TempUnit): number {
  return unit === 'F' ? c * 9 / 5 + 32 : c;
}

export function formatTemp(c: number, unit: TempUnit): string {
  return `${toDisplayTemp(c, unit).toFixed(1)}°${unit}`;
}

export function formatResistance(ohms: number): string {
  if (ohms >= 1000) return `${(ohms / 1000).toFixed(1)} kΩ`;
  return `${Math.round(ohms)} Ω`;
}

export function formatUptime(ms: number): string {
  const s   = Math.floor(ms / 1000);
  const h   = Math.floor(s / 3600);
  const m   = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  if (h > 0) return `${h}h ${m}m ${sec}s`;
  if (m > 0) return `${m}m ${sec}s`;
  return `${sec}s`;
}

export function formatAge(epochMs: number | null): string {
  if (epochMs === null) return '—';
  const ageS = Math.floor((Date.now() - epochMs) / 1000);
  if (ageS < 2)  return 'just now';
  if (ageS < 60) return `${ageS} s ago`;
  return `${Math.floor(ageS / 60)} m ago`;
}
