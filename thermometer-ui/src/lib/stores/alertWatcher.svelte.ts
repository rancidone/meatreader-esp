// alertWatcher — tracks alert_triggered transitions per channel, advances
// cook profile stages, and fires browser notifications.
//
// Not self-contained: call processSnapshot(snap) from an $effect in App.svelte
// so it runs whenever the SSE delivers a new snapshot.
//
// Stage progression:
//   When alert_triggered fires, the watcher advances to the next stage in
//   the assigned profile and patches the config with that stage's target_temp_c.
//   On the final stage the config alert is disabled and the cook is marked done.

import { profileStore } from './profileStore.svelte.ts';
import { config as configApi } from '../api/live.ts';
import type { Snapshot } from '../api/types.ts';

class AlertWatcher {
  // Keyed by channelIdx. True while the last alert transition was false→true
  // and hasn't been acknowledged or cleared by the device.
  alertActive = $state<Record<number, boolean>>({});

  // Non-reactive: previous triggered state per channel.
  #prevTriggered: Record<number, boolean> = {};
  // Non-reactive: current stage index per channel (0-based).
  #stageIdx: Record<number, number> = {};

  processSnapshot(snap: Snapshot): void {
    for (const ch of snap.channels) {
      const idx = ch.id;
      const triggered = ch.alert_triggered ?? false;
      const wasTriggered = this.#prevTriggered[idx] ?? false;

      if (triggered && !wasTriggered) {
        // Transition: alert just fired.
        this.alertActive = { ...this.alertActive, [idx]: true };
        void this.#onAlertFired(idx, ch.temperature_c ?? 0);
      } else if (!triggered && wasTriggered) {
        // Device cleared the alert (temp dropped below hysteresis).
        this.alertActive = { ...this.alertActive, [idx]: false };
      }

      this.#prevTriggered[idx] = triggered;
    }
  }

  // Called when a channel's alert fires. Advances to next stage or marks done.
  async #onAlertFired(channelIdx: number, tempC: number): Promise<void> {
    const ref = profileStore.selectedProfileByChannel.get(channelIdx);
    const profile = ref ? profileStore.resolveProfile(ref) : null;

    const currentStage = this.#stageIdx[channelIdx] ?? 0;

    if (!profile || profile.num_stages === 0) {
      // No profile — just notify the threshold was hit.
      this.#notify(`Ch ${channelIdx} reached target`, `${(tempC * 9/5 + 32).toFixed(1)}°F`);
      return;
    }

    const stageLabel = profile.stages[currentStage]?.label ?? `Stage ${currentStage + 1}`;
    const nextStageIdx = currentStage + 1;

    if (nextStageIdx < profile.stages.length) {
      // Advance to next stage.
      const nextStage = profile.stages[nextStageIdx];
      this.#stageIdx[channelIdx] = nextStageIdx;

      this.#notify(
        `${profile.name} — ${stageLabel} complete`,
        `Advancing to stage ${nextStageIdx + 1}: ${nextStage.label} (${(nextStage.target_temp_c * 9/5 + 32).toFixed(0)}°F)`,
      );

      // Patch the alert target to the next stage's temp.
      try {
        const cfg = await configApi.get();
        const alerts = [...(cfg.alerts ?? [])];
        while (alerts.length <= channelIdx) {
          alerts.push({ enabled: false, target_temp_c: 100, method: 'none', webhook_url: '' });
        }
        alerts[channelIdx] = {
          ...alerts[channelIdx],
          enabled: true,
          target_temp_c: nextStage.target_temp_c,
        };
        await configApi.patch({ alerts });
      } catch {
        // Non-fatal — the notification already fired.
      }
    } else {
      // Final stage done.
      this.#stageIdx[channelIdx] = 0;
      profileStore.clearChannelProfile(channelIdx);

      this.#notify(
        `${profile.name} — cook complete!`,
        `Ch ${channelIdx} reached ${stageLabel} target (${(tempC * 9/5 + 32).toFixed(1)}°F)`,
      );

      // Disable the alert now that the cook is done.
      try {
        const cfg = await configApi.get();
        const alerts = [...(cfg.alerts ?? [])];
        if (alerts[channelIdx]) {
          alerts[channelIdx] = { ...alerts[channelIdx], enabled: false };
          await configApi.patch({ alerts });
        }
      } catch {
        // Non-fatal.
      }
    }
  }

  #notify(title: string, body: string): void {
    // Try browser Notification API; fall back to console.
    if (!('Notification' in window)) return;

    const fire = () => new Notification(title, { body, icon: '/favicon.ico' });

    if (Notification.permission === 'granted') {
      fire();
    } else if (Notification.permission === 'default') {
      void Notification.requestPermission().then(p => { if (p === 'granted') fire(); });
    }
  }

  // Dismiss the blink for a channel (e.g. user clicked the card).
  acknowledge(channelIdx: number): void {
    this.alertActive = { ...this.alertActive, [channelIdx]: false };
  }
}

export const alertWatcher = new AlertWatcher();
