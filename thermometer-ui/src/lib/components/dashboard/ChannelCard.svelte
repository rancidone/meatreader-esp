<script lang="ts">
  import type { ChannelReading, Snapshot } from '../../api/types.ts';
  import type { TempUnit } from '../../stores/ui.svelte.ts';
  import { formatTemp } from '../../utils/format.ts';
  import QualityBadge from '../common/QualityBadge.svelte';
  import Sparkline    from './Sparkline.svelte';

  interface Props {
    reading:   ChannelReading | null;
    snapshots: Snapshot[];
    unit:      TempUnit;
    color:     string;
    onclick?:  () => void;
  }
  const { reading, snapshots, unit, color, onclick }: Props = $props();
</script>

<div class="card channel-card">
  <div class="card-header">
    <h2>Channel {reading?.id ?? '—'}</h2>
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
