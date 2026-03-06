// Config store — fetch-on-demand (not polled).
//
// isDirty is true when staged differs from active.
// All mutation methods re-fetch config after the API call so the UI stays consistent.

import { config as configApi } from '../api/live.ts';
import type { ConfigStatus, DeviceConfigPatch } from '../api/live.ts';

class ConfigStore {
  status  = $state<ConfigStatus | null>(null);
  loading = $state(false);
  error   = $state<string | null>(null);

  isDirty = $derived(
    this.status !== null &&
    JSON.stringify(this.status.active) !== JSON.stringify(this.status.staged)
  );

  async fetch(): Promise<void> {
    this.loading = true;
    this.error   = null;
    try {
      this.status = await configApi.get();
    } catch (e) {
      this.error = e instanceof Error ? e.message : String(e);
    } finally {
      this.loading = false;
    }
  }

  async patchStaged(patch: DeviceConfigPatch): Promise<void> {
    await this.#mutate(() => configApi.patchStaged(patch));
  }

  async apply(): Promise<void> {
    await this.#mutate(() => configApi.apply());
  }

  async commit(): Promise<void> {
    await this.#mutate(() => configApi.commit());
  }

  async revertStaged(): Promise<void> {
    await this.#mutate(() => configApi.revertStaged());
  }

  async revertActive(): Promise<void> {
    await this.#mutate(() => configApi.revertActive());
  }

  async #mutate(action: () => Promise<unknown>): Promise<void> {
    this.loading = true;
    this.error   = null;
    try {
      await action();
      await this.fetch();   // re-fetch so all three states stay in sync
    } catch (e) {
      this.error = e instanceof Error ? e.message : String(e);
    } finally {
      this.loading = false;
    }
  }
}

export const configStore = new ConfigStore();
