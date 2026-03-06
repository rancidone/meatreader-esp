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
    onclose:  () => void;
  }
  const { history, unit, colors, onclose }: Props = $props();

  let container = $state<HTMLDivElement | undefined>(undefined);
  // $state so the data-update effect can track when the instance becomes ready.
  let uplotInstance = $state<uPlot | null>(null);

  // Build aligned uPlot data arrays from history (recomputes when history/unit change).
  const chartData = $derived.by((): uPlot.AlignedData => {
    const ts:  number[]         = [];
    const ch0: (number | null)[] = [];
    const ch1: (number | null)[] = [];
    for (const snap of history) {
      ts.push(snap.timestamp / 1000);   // uPlot uses Unix seconds
      const c0 = snap.channels.find(c => c.id === 0);
      const c1 = snap.channels.find(c => c.id === 1);
      ch0.push(c0?.quality === 'ok' ? toDisplayTemp(c0.temperature_c, unit) : null);
      ch1.push(c1?.quality === 'ok' ? toDisplayTemp(c1.temperature_c, unit) : null);
    }
    return [ts, ch0, ch1];
  });

  // Create/destroy the uPlot instance when the container mounts or unit changes.
  // Intentionally does NOT read chartData, so it only runs once per mount.
  $effect(() => {
    const el = container;
    if (!el) return;

    // Reading `unit` here so the chart is recreated when unit toggles (axis label).
    const yLabel = `°${unit}`;

    const opts: uPlot.Options = {
      width:  el.clientWidth  || 800,
      height: el.clientHeight || 380,
      tzDate: ts => uPlot.tzDate(new Date(ts * 1e3), Intl.DateTimeFormat().resolvedOptions().timeZone),
      series: [
        {},
        {
          label:    'Ch 0',
          stroke:   colors[0],
          fill:     colors[0] + '22',
          width:    1.5,
          spanGaps: false,
        },
        {
          label:    'Ch 1',
          stroke:   colors[1],
          fill:     colors[1] + '22',
          width:    1.5,
          spanGaps: false,
        },
      ],
      axes: [
        {
          values: (_u, ticks) =>
            ticks.map(t => new Date(t * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })),
        },
        { label: yLabel },
      ],
      cursor: { drag: { x: true, y: false } },
    };

    // Start with empty data; the data-update effect below fills it in.
    const instance = new uPlot(opts, [[], [], []], el);
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
    max-height: 90vh;
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
    height: 400px;
    padding: 0.5rem 1rem 1rem;
    box-sizing: border-box;
  }

  :global(.chart-container .uplot) {
    width: 100% !important;
  }
</style>
