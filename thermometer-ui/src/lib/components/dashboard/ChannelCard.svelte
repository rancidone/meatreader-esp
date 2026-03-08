<script lang="ts">
  import type { ChannelReading, Snapshot } from '../../api/types.ts';
  import type { TempUnit } from '../../stores/ui.svelte.ts';
  import type { ChannelPrediction } from '../../stores/predictions.svelte.ts';
  import { formatTemp, formatResistance } from '../../utils/format.ts';
  import QualityBadge from '../common/QualityBadge.svelte';
  import Sparkline    from './Sparkline.svelte';
  import { cookStore } from '../../stores/cookStore.svelte.ts';

  interface Props {
    reading:     ChannelReading | null;
    snapshots:   Snapshot[];
    unit:        TempUnit;
    color:       string;
    prediction?: ChannelPrediction;
    onclick?:    () => void;
  }
  const { reading, snapshots, unit, color, prediction, onclick }: Props = $props();

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
    <h2>Channel {reading?.id ?? '—'}</h2>
    {#if reading}
      <QualityBadge quality={reading.quality} />
    {:else}
      <span class="badge disabled">—</span>
    {/if}
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

  {#if reading?.raw_adc !== undefined || reading?.resistance_ohms !== undefined}
  <dl class="meta">
    {#if reading?.raw_adc !== undefined}
    <div class="meta-row">
      <dt>ADC</dt>
      <dd>{reading.raw_adc}</dd>
    </div>
    {/if}
    {#if reading?.resistance_ohms !== undefined}
    <div class="meta-row">
      <dt>Resistance</dt>
      <dd>{formatResistance(reading.resistance_ohms)}</dd>
    </div>
    {/if}
  </dl>
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

  /* ── Meta ────────────────────────────────────────────────────────────────── */

  .meta {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: var(--gap-xs);
    margin-bottom: var(--gap-xs);
  }

  .meta-row {
    background: var(--color-surface-alt);
    border-radius: var(--radius);
    padding: 0.35rem 0.6rem;
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

  dt {
    font-size: 0.7rem;
    color: var(--color-text-muted);
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  dd {
    font-size: 0.9rem;
    font-family: var(--font-mono);
    margin: 0;
  }
</style>
