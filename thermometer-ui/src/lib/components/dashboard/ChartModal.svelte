<script lang="ts">
  import uPlot from 'uplot';
  import 'uplot/dist/uPlot.min.css';
  import type { Snapshot } from '../../api/types.ts';
  import type { TempUnit } from '../../stores/ui.svelte.ts';
  import { toDisplayTemp } from '../../utils/format.ts';

  interface Props {
    history:  Snapshot[];
    unit:     TempUnit;
    colors:   readonly string[];
    channelIds: readonly number[];
    labels: readonly string[];
    onclose:  () => void;
  }
  const { history, unit, colors, channelIds, labels, onclose }: Props = $props();

  let container = $state<HTMLDivElement | undefined>(undefined);
  // $state so the data-update effect can track when the instance becomes ready.
  let uplotInstance = $state<uPlot | null>(null);
  const minDisplaySpan = $derived(unit === 'F' ? 16 : 9);  // keep trend visible at low noise

  // Build aligned uPlot data arrays from history (recomputes when history/unit change).
  const chartData = $derived.by((): uPlot.AlignedData => {
    const ts: number[] = [];
    const channels = channelIds.map((): (number | null)[] => []);
    for (const snap of history) {
      ts.push(snap.timestamp / 1000);   // uPlot uses Unix seconds
      for (let index = 0; index < channelIds.length; index++) {
        const reading = snap.channels.find(channel => channel.id === channelIds[index]);
        channels[index].push(
          reading?.quality === 'ok' ? toDisplayTemp(reading.temperature_c, unit) : null
        );
      }
    }
    return [ts, ...channels];
  });

  // Create/destroy the uPlot instance when the container mounts or unit changes.
  // Intentionally does NOT read chartData, so it only runs once per mount.
  $effect(() => {
    const el = container;
    if (!el) return;

    // Reading these props recreates the chart if its series set changes.
    const yLabel = `°${unit}`;
    const series = channelIds.map((channelId) => ({
      label: labels[channelId] || `Ch ${channelId}`,
      stroke: colors[channelId % colors.length],
      width: 2,
      spanGaps: false,
    }));

    const opts: uPlot.Options = {
      width:  el.clientWidth  || 800,
      height: el.clientHeight || 380,
      tzDate: ts => uPlot.tzDate(new Date(ts * 1e3), Intl.DateTimeFormat().resolvedOptions().timeZone),
      scales: {
        y: {
          auto: true,
          range: (_u, min, max) => {
            const low = Number.isFinite(min) ? min : 0;
            const high = Number.isFinite(max) ? max : low;
            const span = high - low;
            const baseSpan = Math.max(span, minDisplaySpan);
            const pad = baseSpan * 0.12;
            const mid = (low + high) / 2;
            return [mid - baseSpan / 2 - pad, mid + baseSpan / 2 + pad];
          },
        },
      },
      series: [
        {},
        ...series,
      ],
      axes: [
        {
          values: (_u, ticks) =>
            ticks.map(t => new Date(t * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })),
          stroke: 'rgba(180,190,210,0.7)',
          ticks:  { stroke: 'rgba(180,190,210,0.15)' },
          grid:   { stroke: 'rgba(180,190,210,0.08)' },
        },
        {
          label:  yLabel,
          stroke: 'rgba(180,190,210,0.7)',
          ticks:  { stroke: 'rgba(180,190,210,0.15)' },
          grid:   { stroke: 'rgba(180,190,210,0.08)' },
        },
      ],
      cursor: { drag: { x: true, y: false } },
    };

    // Start with empty data; the data-update effect below fills it in.
    const instance = new uPlot(
      opts,
      Array.from({ length: channelIds.length + 1 }, () => []) as uPlot.AlignedData,
      el
    );
    uplotInstance = instance;

    return () => {
      instance.destroy();
      uplotInstance = null;
    };
  });

  // Push new data into the existing instance whenever chartData or the instance changes.
  $effect(() => {
    if (uplotInstance) {
      uplotInstance.setData(chartData);
    }
  });

  function onKeydown(e: KeyboardEvent) {
    if (e.key === 'Escape') onclose();
  }

  function onBackdrop(e: MouseEvent) {
    if (e.target === e.currentTarget) onclose();
  }
</script>

<svelte:window onkeydown={onKeydown} />

<!-- svelte-ignore a11y_click_events_have_key_events a11y_no_static_element_interactions -->
<div class="overlay" onclick={onBackdrop}>
  <div class="modal">
    <div class="modal-header">
      <span class="modal-title">Temperature History</span>
      <button class="close-btn" onclick={onclose}>✕</button>
    </div>
    <div class="chart-container" bind:this={container}></div>
  </div>
</div>

<style>
  .overlay {
    position: fixed;
    inset: 0;
    z-index: 100;
    background: rgba(0, 0, 0, 0.7);
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 1rem;
  }

  .modal {
    background: var(--color-surface);
    border: 1px solid var(--color-border);
    border-radius: var(--radius);
    width: 100%;
    max-width: 960px;
    height: min(600px, 90vh);
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }

  .modal-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0.75rem 1rem;
    border-bottom: 1px solid var(--color-border);
    flex-shrink: 0;
  }

  .modal-title {
    font-size: 0.9rem;
    font-weight: 600;
    color: var(--color-text-muted);
    text-transform: uppercase;
    letter-spacing: 0.06em;
  }

  .close-btn {
    font-size: 1rem;
    padding: 0.2rem 0.5rem;
    font-family: var(--font-mono);
    line-height: 1;
  }

  .chart-container {
    flex: 1;
    min-height: 0;
    width: 100%;
    padding: 0.5rem 1rem 1rem;
    box-sizing: border-box;
  }

  :global(.chart-container .uplot) {
    width: 100% !important;
  }

  :global(.chart-container .u-axis .u-label),
  :global(.chart-container .u-axis .u-value),
  :global(.chart-container .u-legend .u-label),
  :global(.chart-container .u-legend td) {
    color: var(--color-text-muted) !important;
  }

  :global(.chart-container .u-axis line),
  :global(.chart-container .u-axis path) {
    stroke: var(--color-border) !important;
  }
</style>
