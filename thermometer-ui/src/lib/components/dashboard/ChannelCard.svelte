<script lang="ts">
  import type { ChannelReading, Snapshot } from '../../api/types.ts';
  import type { TempUnit } from '../../stores/ui.svelte.ts';
  import { formatTemp, formatResistance } from '../../utils/format.ts';
  import QualityBadge from '../common/QualityBadge.svelte';
  import Sparkline    from './Sparkline.svelte';

  interface Props {
    reading:   ChannelReading | null;
    snapshots: Snapshot[];
    unit:      TempUnit;
    color:     string;
    label?:    string;
    onclick?:  () => void;
  }
  const { reading, snapshots, unit, color, label, onclick }: Props = $props();
</script>

<div class="card channel-card">
  <div class="card-header">
    <h2>{label || `Channel ${reading?.id ?? '—'}`}</h2>
    {#if reading}
      <QualityBadge quality={reading.quality} />
    {:else}
      <span class="badge disabled">—</span>
    {/if}
  </div>

  <div class="temp" style="color: {reading?.quality === 'ok' ? color : 'var(--color-text-muted)'}">
    {reading?.quality === 'ok' ? formatTemp(reading.temperature_c, unit) : '— °' + unit}
  </div>

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
