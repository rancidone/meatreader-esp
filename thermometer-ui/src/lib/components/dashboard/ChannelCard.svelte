<script lang="ts">
  import type { ChannelReading, Snapshot } from '../../api/types.ts';
  import type { TempUnit } from '../../stores/ui.svelte.ts';
  import type { ChannelPrediction } from '../../stores/predictions.svelte.ts';
  import { formatTemp } from '../../utils/format.ts';
  import QualityBadge from '../common/QualityBadge.svelte';
  import Sparkline    from './Sparkline.svelte';
  import { cookStore } from '../../stores/cookStore.svelte.ts';

  interface Props {
    reading:     ChannelReading | null;
    snapshots:   Snapshot[];
    unit:        TempUnit;
    color:       string;
    label?:      string;
    prediction?: ChannelPrediction;
    onclick?:    () => void;
  }
  const { reading, snapshots, unit, color, label, prediction, onclick }: Props = $props();

  // ── Cook timer ──────────────────────────────────────────────────────────────

  let nowMs = $state(Date.now());

  $effect(() => {
    const id = setInterval(() => { nowMs = Date.now(); }, 1000);
    return () => clearInterval(id);
  });

  const channelId = $derived(reading?.id ?? -1);

  const cookStart = $derived(
    channelId >= 0 ? (cookStore.cookStartTime[channelId] ?? null) : null
  );

  const elapsedMs = $derived(
    cookStart !== null ? Math.max(0, nowMs - cookStart) : null
  );

  function formatElapsed(ms: number): string {
    const totalSec = Math.floor(ms / 1000);
    const h   = Math.floor(totalSec / 3600);
    const m   = Math.floor((totalSec % 3600) / 60);
    const s   = totalSec % 60;
    return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
  }

  // ── Predicted done time ──────────────────────────────────────────────────────

  function formatEta(ms: number): string {
    const totalMin = Math.floor(ms / 60000);
    const h = Math.floor(totalMin / 60);
    const m = totalMin % 60;
    if (h > 0) return `${h}h ${m}m`;
    return `${m}m`;
  }

  const etaMs = $derived(prediction?.eta_ms ?? null);
</script>

<div class="card channel-card">
  <div class="card-header">
    <h2>{label || `Channel ${reading?.id ?? '—'}`}</h2>
    <div class="badge-group">
      {#if reading?.alert_triggered === true}
        <span class="alert-badge">Alert!</span>
      {/if}
      {#if reading}
        <QualityBadge quality={reading.quality} />
      {:else}
        <span class="badge disabled">—</span>
      {/if}
    </div>
  </div>

  <div class="temp" style="color: {reading?.quality === 'ok' ? color : 'var(--color-text-muted)'}">
    {reading?.quality === 'ok' ? formatTemp(reading.temperature_c, unit) : '— °' + unit}
  </div>

  <!-- Cook timer display & controls -->
  {#if reading?.quality === 'ok'}
    <div class="cook-row">
      {#if elapsedMs !== null}
        <span class="cook-elapsed" style="color: {color}">
          {formatElapsed(elapsedMs)}
        </span>
        <button
          class="cook-btn stop"
          onclick={() => cookStore.stopCook(channelId)}
        >Stop cook</button>
      {:else}
        <button
          class="cook-btn start"
          onclick={() => cookStore.startCook(channelId)}
        >Start cook</button>
      {/if}
    </div>
  {/if}

  <!-- Predicted done time -->
  {#if etaMs !== null}
    <div class="eta-row">
      <span class="eta-label">Done in ~{formatEta(etaMs)}</span>
    </div>
  {/if}

  <!-- svelte-ignore a11y_click_events_have_key_events a11y_no_static_element_interactions -->
  <div class="sparkline-wrap" class:clickable={!!onclick} {onclick}>
    <Sparkline {snapshots} channelId={reading?.id ?? 0} {unit} {color} />
    {#if onclick}
      <span class="expand-hint">expand ↗</span>
    {/if}
  </div>
</div>

<style>
  .channel-card {
    display: flex;
    flex-direction: column;
    gap: var(--gap-sm);
  }

  .card-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .badge-group {
    display: flex;
    align-items: center;
    gap: var(--gap-xs);
  }

  .alert-badge {
    display: inline-flex;
    align-items: center;
    gap: 0.2em;
    padding: 0.2rem 0.5rem;
    border-radius: var(--radius);
    background: color-mix(in srgb, var(--color-error) 15%, var(--color-surface-alt));
    border: 1px solid var(--color-error);
    color: var(--color-error);
    font-size: 0.72rem;
    font-weight: 600;
    letter-spacing: 0.03em;
    text-transform: uppercase;
  }

  .temp {
    font-size: 2.4rem;
    font-weight: 700;
    letter-spacing: -0.02em;
    line-height: 1;
    padding: 0.25rem 0;
  }

  /* ── Cook timer ──────────────────────────────────────────────────────────── */

  .cook-row {
    display: flex;
    align-items: center;
    gap: var(--gap-sm);
  }

  .cook-elapsed {
    font-family: var(--font-mono);
    font-size: 1.1rem;
    font-weight: 600;
    letter-spacing: 0.04em;
    flex: 1;
  }

  .cook-btn {
    font-size: 0.75rem;
    padding: 0.2rem 0.6rem;
    font-family: var(--font-mono);
    border-radius: var(--radius);
    cursor: pointer;
  }

  .cook-btn.start {
    background: transparent;
    border: 1px solid var(--color-border);
    color: var(--color-text-muted);
  }

  .cook-btn.start:hover {
    border-color: var(--color-text);
    color: var(--color-text);
  }

  .cook-btn.stop {
    background: color-mix(in srgb, var(--color-error) 12%, transparent);
    border: 1px solid color-mix(in srgb, var(--color-error) 40%, transparent);
    color: var(--color-error);
  }

  /* ── ETA ─────────────────────────────────────────────────────────────────── */

  .eta-row {
    font-size: 0.82rem;
    color: var(--color-text-muted);
  }

  .eta-label {
    font-family: var(--font-mono);
  }

  .sparkline-wrap {
    position: relative;
  }

  .sparkline-wrap.clickable {
    cursor: pointer;
  }

  .expand-hint {
    position: absolute;
    bottom: 4px;
    right: 6px;
    font-size: 0.65rem;
    color: var(--color-text-muted);
    opacity: 0;
    transition: opacity 0.15s;
    pointer-events: none;
  }

  .sparkline-wrap.clickable:hover .expand-hint {
    opacity: 1;
  }
</style>
