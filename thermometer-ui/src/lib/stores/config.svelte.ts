// Config store — fetch-on-demand (not polled).

import { config as configApi } from '../api/live.ts';
import type { DeviceConfig, DeviceConfigPatch } from '../api/live.ts';

class ConfigStore {
  status  = $state<DeviceConfig | null>(null);
  loading = $state(false);
  error   = $state<string | null>(null);

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

  async patch(body: DeviceConfigPatch): Promise<void> {
    await this.#mutate(async () => {
      const res = await configApi.patch(body);
      this.status = res.config;
    });
  }

  async commit(): Promise<void> {
    await this.#mutate(() => configApi.commit());
  }

  async #mutate(action: () => Promise<unknown>): Promise<void> {
    this.loading = true;
    this.error   = null;
    try {
      await action();
      if (!this.status) await this.fetch();
    } catch (e) {
      this.error = e instanceof Error ? e.message : String(e);
    } finally {
      this.loading = false;
    }
  }
}

export const configStore = new ConfigStore();
