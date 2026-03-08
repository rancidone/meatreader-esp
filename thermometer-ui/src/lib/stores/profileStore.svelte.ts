// Profile store — fetches cook profiles from /profiles, manages selection per
// channel in localStorage, and provides a read-only starter library of common
// BBQ presets.
//
// Usage:
//   profileStore.profiles            ← CookProfile[8] from API (name="" = empty slot)
//   profileStore.starterLibrary      ← read-only preset profiles
//   profileStore.selectedProfileByChannel  ← Map<channelIdx, profileIdx | 'starter:N'>
//   profileStore.fetchProfiles()
//   profileStore.saveProfile(id, profile)
//   profileStore.deleteProfile(id)
//   profileStore.applyProfileToChannel(channelIdx, profileIdx)

import { config as configApi } from '../api/live.ts';
import type { CookProfile, ProfilesResponse } from '../api/types.ts';
import { ApiError } from '../api/types.ts';

// ── Temperature helpers ────────────────────────────────────────────────────

function fToC(f: number): number {
  return (f - 32) * 5 / 9;
}

// ── Starter library ────────────────────────────────────────────────────────
// Temps stored as °C internally (converted from USDA °F).
// Displayed as °F in the UI via formatTemp.

export const STARTER_LIBRARY: CookProfile[] = [
  {
    name: 'Brisket',
    num_stages: 2,
    stages: [
      { label: 'Wrap', target_temp_c: fToC(165), alert_method: 'none' },
      { label: 'Finish', target_temp_c: fToC(203), alert_method: 'none' },
    ],
  },
  {
    name: 'Pork Shoulder',
    num_stages: 2,
    stages: [
      { label: 'Wrap', target_temp_c: fToC(165), alert_method: 'none' },
      { label: 'Finish', target_temp_c: fToC(200), alert_method: 'none' },
    ],
  },
  {
    name: 'Chicken Breast',
    num_stages: 1,
    stages: [
      { label: 'Done (USDA)', target_temp_c: fToC(165), alert_method: 'none' },
    ],
  },
  {
    name: 'Medium-Rare Beef',
    num_stages: 1,
    stages: [
      { label: 'Pull temp', target_temp_c: fToC(130), alert_method: 'none' },
    ],
  },
  {
    name: 'Medium Beef',
    num_stages: 1,
    stages: [
      { label: 'Pull temp', target_temp_c: fToC(145), alert_method: 'none' },
    ],
  },
];

// ── localStorage persistence ───────────────────────────────────────────────

const LS_KEY = 'meatreader:profileByChannel';

// profileRef: 'api:N' for slot N in /profiles, 'starter:N' for starter library
export type ProfileRef = `api:${number}` | `starter:${number}`;

function loadSelectionFromStorage(): Map<number, ProfileRef> {
  try {
    const raw = localStorage.getItem(LS_KEY);
    if (!raw) return new Map();
    const obj = JSON.parse(raw) as Record<string, string>;
    return new Map(
      Object.entries(obj).map(([k, v]) => [Number(k), v as ProfileRef])
    );
  } catch {
    return new Map();
  }
}

function saveSelectionToStorage(map: Map<number, ProfileRef>): void {
  const obj: Record<string, string> = {};
  for (const [ch, ref] of map) {
    obj[String(ch)] = ref;
  }
  localStorage.setItem(LS_KEY, JSON.stringify(obj));
}

// ── Profile API helpers ────────────────────────────────────────────────────

async function fetchJsonProfiles<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(path, {
    headers: { 'Content-Type': 'application/json', ...init?.headers },
    ...init,
  });
  if (!res.ok) {
    let message = res.statusText;
    try {
      const body = await res.json() as { message?: string };
      if (body.message) message = body.message;
    } catch { /* ignore */ }
    throw new ApiError(res.status, message);
  }
  return res.json() as Promise<T>;
}

// ── Store class ────────────────────────────────────────────────────────────

class ProfileStore {
  profiles    = $state<CookProfile[]>([]);
  loading     = $state(false);
  error       = $state<string | null>(null);

  // starterLibrary is read-only — exposed for convenience
  readonly starterLibrary: CookProfile[] = STARTER_LIBRARY;

  // Map from channelIdx → ProfileRef
  #selection = $state<Map<number, ProfileRef>>(loadSelectionFromStorage());

  get selectedProfileByChannel(): Map<number, ProfileRef> {
    return this.#selection;
  }

  // ── Resolve a ProfileRef to a CookProfile ──────────────────────────────

  resolveProfile(ref: ProfileRef): CookProfile | null {
    if (ref.startsWith('starter:')) {
      const idx = Number(ref.slice(8));
      return STARTER_LIBRARY[idx] ?? null;
    }
    if (ref.startsWith('api:')) {
      const idx = Number(ref.slice(4));
      const p = this.profiles[idx];
      return p && p.name ? p : null;
    }
    return null;
  }

  getSelectedProfile(channelIdx: number): CookProfile | null {
    const ref = this.#selection.get(channelIdx);
    if (!ref) return null;
    return this.resolveProfile(ref);
  }

  // ── Fetch ──────────────────────────────────────────────────────────────

  async fetchProfiles(): Promise<void> {
    this.loading = true;
    this.error   = null;
    try {
      const data = await fetchJsonProfiles<ProfilesResponse>('/profiles');
      this.profiles = data;
    } catch (e) {
      // /profiles endpoint may not exist yet in firmware — treat as empty
      if (e instanceof ApiError && e.httpStatus === 404) {
        this.profiles = [];
      } else {
        this.error = e instanceof Error ? e.message : String(e);
      }
    } finally {
      this.loading = false;
    }
  }

  // ── Save a profile slot ────────────────────────────────────────────────

  async saveProfile(id: number, profile: CookProfile): Promise<void> {
    this.loading = true;
    this.error   = null;
    try {
      await fetchJsonProfiles(`/profiles/${id}`, {
        method: 'PUT',
        body: JSON.stringify(profile),
      });
      await this.fetchProfiles();
    } catch (e) {
      this.error = e instanceof Error ? e.message : String(e);
    } finally {
      this.loading = false;
    }
  }

  // ── Delete a profile slot ──────────────────────────────────────────────

  async deleteProfile(id: number): Promise<void> {
    this.loading = true;
    this.error   = null;
    try {
      await fetchJsonProfiles(`/profiles/${id}`, { method: 'DELETE' });
      await this.fetchProfiles();
    } catch (e) {
      this.error = e instanceof Error ? e.message : String(e);
    } finally {
      this.loading = false;
    }
  }

  // ── Apply a profile to a channel ────────────────────────────────────────
  // Sets the channel's alert target to the first stage target_temp_c,
  // patches staged config and applies it.

  async applyProfileToChannel(channelIdx: number, ref: ProfileRef): Promise<void> {
    const profile = this.resolveProfile(ref);
    if (!profile || profile.num_stages === 0 || profile.stages.length === 0) {
      this.error = 'Profile has no stages';
      return;
    }

    // Persist selection
    const next = new Map(this.#selection);
    next.set(channelIdx, ref);
    this.#selection = next;
    saveSelectionToStorage(next);

    // Update alert target for this channel via config API
    const firstStageTargetC = profile.stages[0].target_temp_c;
    this.loading = true;
    this.error   = null;
    try {
      // We need current alerts to build the patch without clobbering other channels.
      // We'll fetch current config first.
      const cfgStatus = await configApi.get();
      const currentAlerts = cfgStatus.staged.alerts ?? [];

      // Build updated alerts array — ensure slot exists for channelIdx
      const updatedAlerts = [...currentAlerts];
      while (updatedAlerts.length <= channelIdx) {
        updatedAlerts.push({
          enabled: false,
          target_temp_c: 100,
          method: 'none',
          webhook_url: '',
        });
      }
      updatedAlerts[channelIdx] = {
        ...updatedAlerts[channelIdx],
        enabled: true,
        target_temp_c: firstStageTargetC,
      };

      await configApi.patchStaged({ alerts: updatedAlerts });
      await configApi.apply();
    } catch (e) {
      this.error = e instanceof Error ? e.message : String(e);
    } finally {
      this.loading = false;
    }
  }

  // ── Clear channel selection ────────────────────────────────────────────

  clearChannelProfile(channelIdx: number): void {
    const next = new Map(this.#selection);
    next.delete(channelIdx);
    this.#selection = next;
    saveSelectionToStorage(next);
  }
}

export const profileStore = new ProfileStore();
