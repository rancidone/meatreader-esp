import { describe, it, expect, vi, beforeEach } from 'vitest';

// Mock the Svelte store module that uses $state runes — not available outside
// the Svelte compiler.  We only need the TempUnit type; the mock keeps the
// runtime import from crashing.
vi.mock('../stores/ui.svelte.ts', () => ({
  ui: { unit: 'C', polling: true, historySize: 300 },
}));

import {
  toDisplayTemp,
  formatTemp,
  formatResistance,
  formatUptime,
  formatAge,
} from './format';

// ── toDisplayTemp ─────────────────────────────────────────────────────────────

describe('toDisplayTemp', () => {
  it('returns Celsius unchanged when unit is C', () => {
    expect(toDisplayTemp(100, 'C')).toBe(100);
    expect(toDisplayTemp(0, 'C')).toBe(0);
    expect(toDisplayTemp(-40, 'C')).toBe(-40);
  });

  it('converts Celsius to Fahrenheit when unit is F', () => {
    expect(toDisplayTemp(0, 'F')).toBeCloseTo(32, 5);
    expect(toDisplayTemp(100, 'F')).toBeCloseTo(212, 5);
    expect(toDisplayTemp(-40, 'F')).toBeCloseTo(-40, 5);
  });

  it('handles non-integer Celsius values', () => {
    expect(toDisplayTemp(37.5, 'F')).toBeCloseTo(99.5, 5);
  });
});

// ── formatTemp ────────────────────────────────────────────────────────────────

describe('formatTemp', () => {
  it('formats Celsius with one decimal place and °C suffix', () => {
    expect(formatTemp(100, 'C')).toBe('100.0°C');
    expect(formatTemp(0, 'C')).toBe('0.0°C');
    expect(formatTemp(-10.25, 'C')).toBe('-10.3°C');
  });

  it('formats Fahrenheit with one decimal place and °F suffix', () => {
    expect(formatTemp(0, 'F')).toBe('32.0°F');
    expect(formatTemp(100, 'F')).toBe('212.0°F');
    expect(formatTemp(-40, 'F')).toBe('-40.0°F');
  });

  it('rounds to one decimal', () => {
    // 37.55°C → 99.59°F, rounds to 99.6
    expect(formatTemp(37.55, 'F')).toBe('99.6°F');
  });
});

// ── formatResistance ──────────────────────────────────────────────────────────

describe('formatResistance', () => {
  it('formats values below 1000 ohms as whole ohms', () => {
    expect(formatResistance(470)).toBe('470 Ω');
    expect(formatResistance(0)).toBe('0 Ω');
    expect(formatResistance(999)).toBe('999 Ω');
  });

  it('rounds sub-1000 values to nearest integer', () => {
    expect(formatResistance(470.7)).toBe('471 Ω');
    expect(formatResistance(470.3)).toBe('470 Ω');
  });

  it('formats values at 1000 ohms and above as kΩ with one decimal', () => {
    expect(formatResistance(1000)).toBe('1.0 kΩ');
    expect(formatResistance(22100)).toBe('22.1 kΩ');
    expect(formatResistance(10000)).toBe('10.0 kΩ');
  });

  it('handles large resistances', () => {
    expect(formatResistance(1000000)).toBe('1000.0 kΩ');
  });
});

// ── formatUptime ──────────────────────────────────────────────────────────────

describe('formatUptime', () => {
  it('formats sub-minute durations as seconds only', () => {
    expect(formatUptime(0)).toBe('0s');
    expect(formatUptime(1000)).toBe('1s');
    expect(formatUptime(59000)).toBe('59s');
  });

  it('formats minute-level durations without hours', () => {
    expect(formatUptime(60000)).toBe('1m 0s');
    expect(formatUptime(90000)).toBe('1m 30s');
    expect(formatUptime(3599000)).toBe('59m 59s');
  });

  it('formats hour-level durations with hours, minutes, and seconds', () => {
    expect(formatUptime(3600000)).toBe('1h 0m 0s');
    expect(formatUptime(3661000)).toBe('1h 1m 1s');
    expect(formatUptime(86400000)).toBe('24h 0m 0s');
  });
});

// ── formatAge ─────────────────────────────────────────────────────────────────

describe('formatAge', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-01-01T00:01:00.000Z'));
  });

  it('returns em-dash for null input', () => {
    expect(formatAge(null)).toBe('—');
  });

  it('returns "just now" for 0–1 seconds ago', () => {
    const now = Date.now();
    expect(formatAge(now)).toBe('just now');
    expect(formatAge(now - 1000)).toBe('just now');
  });

  it('returns seconds for 2–59 seconds ago', () => {
    const now = Date.now();
    expect(formatAge(now - 2000)).toBe('2 s ago');
    expect(formatAge(now - 59000)).toBe('59 s ago');
  });

  it('returns minutes for 60+ seconds ago', () => {
    const now = Date.now();
    expect(formatAge(now - 60000)).toBe('1 m ago');
    expect(formatAge(now - 120000)).toBe('2 m ago');
  });
});
