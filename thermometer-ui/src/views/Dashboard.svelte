<script lang="ts">
  import { tempsStore }  from '../lib/stores/temps.svelte.ts';
  import { deviceStore } from '../lib/stores/device.svelte.ts';
  import { configStore } from '../lib/stores/config.svelte.ts';
  import { ui }          from '../lib/stores/ui.svelte.ts';
  import { predictions } from '../lib/stores/predictions.svelte.ts';
  import { formatAge }   from '../lib/utils/format.ts';
  import ChannelCard     from '../lib/components/dashboard/ChannelCard.svelte';
  import ChartModal      from '../lib/components/dashboard/ChartModal.svelte';

  // Channel accent colours — ch0 orange, ch1 sky blue.
  const COLORS = ['#f97316', '#38bdf8'] as const;

  let chartOpen = $state(false);

  $effect(() => {
    if (ui.polling) {
      tempsStore.start();
      deviceStore.start();
      return () => {
        tempsStore.stop();
        deviceStore.stop();
      };
    } else {
      tempsStore.stop();
      deviceStore.stop();
    }
  });

  $effect(() => { void configStore.fetch(); });

  // Per-channel readings from the latest snapshot; null when no data yet.
  const readings = $derived(
    tempsStore.latest ? tempsStore.latest.channels : [null, null]
  );
</script>

<div class="col">

  <!-- ── Status bar ───────────────────────────────────────────────────── -->
  <div class="status-bar">
    <div class="row">
      <span class="dot" class:connected={deviceStore.connected}></span>
      <span class="muted">{deviceStore.connected ? 'Connected' : 'Disconnected'}</span>
      {#if deviceStore.statusData?.firmware}
        <span class="muted">· {deviceStore.statusData.firmware}</span>
      {/if}
    </div>

    <div class="row">
      <span class="muted">{ui.polling ? `Updated ${formatAge(tempsStore.lastUpdated)}` : 'Paused'}</span>
      <select
        class="history-select"
        value={ui.historySize}
        onchange={e => ui.setHistorySize(Number((e.target as HTMLSelectElement).value))}
      >
        <option value={60}>1 min</option>
        <option value={300}>5 min</option>
        <option value={900}>15 min</option>
        <option value={1800}>30 min</option>
        <option value={3600}>1 hr</option>
      </select>
      <button class="unit-toggle" onclick={() => chartOpen = true}>Chart</button>
      <button class="unit-toggle" onclick={() => ui.toggleUnit()}>
        Show °{ui.unit === 'F' ? 'C' : 'F'}
      </button>
      <button class="unit-toggle" class:paused={!ui.polling} onclick={() => ui.togglePolling()}>
        {ui.polling ? 'Pause' : 'Resume'}
      </button>
    </div>
  </div>

  <!-- ── Error banner ─────────────────────────────────────────────────── -->
  {#if tempsStore.error}
    <div class="error-banner">{tempsStore.error}</div>
  {/if}

  <!-- ── Channel cards ────────────────────────────────────────────────── -->
  <div class="card-grid">
    {#each [0, 1] as idx}
      <ChannelCard
        reading={readings[idx] ?? null}
        snapshots={tempsStore.history}
        unit={ui.unit}
        color={COLORS[idx]}
        label={configStore.status?.active.channels[idx]?.label}
        channelIdx={idx}
        prediction={predictions.forChannel(idx)}
        onclick={() => chartOpen = true}
      />
    {/each}
  </div>

  <!-- ── Health flags ──────────────────────────────────────────────────── -->
  {#if tempsStore.latest?.health_flags && tempsStore.latest.health_flags.length > 0}
    <div class="health-flags">
      {#each tempsStore.latest.health_flags as flag}
        <span class="badge warn">{flag}</span>
      {/each}
    </div>
  {/if}

</div>

{#if chartOpen}
  <ChartModal
    history={tempsStore.history}
    unit={ui.unit}
    colors={COLORS}
    onclose={() => chartOpen = false}
  />
{/if}

<style>
  .status-bar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    flex-wrap: wrap;
    gap: var(--gap-sm);
    padding-bottom: var(--gap-sm);
    border-bottom: 1px solid var(--color-border);
  }

  .dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: var(--color-error);
    flex-shrink: 0;
  }

  .dot.connected {
    background: var(--color-ok);
    box-shadow: 0 0 6px var(--color-ok);
  }

  .unit-toggle {
    font-size: 0.8rem;
    padding: 0.2rem 0.6rem;
    font-family: var(--font-mono);
  }

  .unit-toggle.paused {
    background: color-mix(in srgb, var(--color-warn) 20%, var(--color-surface-alt));
    border-color: var(--color-warn);
    color: var(--color-warn);
  }

  .error-banner {
    background: color-mix(in srgb, var(--color-error) 12%, transparent);
    border: 1px solid color-mix(in srgb, var(--color-error) 30%, transparent);
    color: var(--color-error);
    border-radius: var(--radius);
    padding: 0.5rem 0.85rem;
    font-size: 0.85rem;
  }

  .card-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
    gap: var(--gap);
  }

  .health-flags {
    display: flex;
    flex-wrap: wrap;
    gap: var(--gap-xs);
  }

  .history-select {
    font-size: 0.8rem;
    padding: 0.2rem 0.4rem;
    font-family: var(--font-mono);
    background: var(--color-surface-alt);
    border: 1px solid var(--color-border);
    border-radius: var(--radius);
    color: var(--color-text);
    cursor: pointer;
  }
</style>
