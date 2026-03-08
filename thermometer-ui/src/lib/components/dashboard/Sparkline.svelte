<script lang="ts">
  import type { Snapshot } from '../../api/types.ts';
  import type { TempUnit } from '../../stores/ui.svelte.ts';
  import { toDisplayTemp } from '../../utils/format.ts';

  interface Props {
    snapshots:  Snapshot[];
    channelId:  number;
    unit:       TempUnit;
    color:      string;
  }
  const { snapshots, channelId, unit, color }: Props = $props();

  let canvas = $state<HTMLCanvasElement | undefined>(undefined);
  const minDisplaySpan = $derived(unit === 'F' ? 10 : 6);

  $effect(() => {
    const el = canvas;
    if (!el) return;

    // Extract points — reading reactive deps here makes the effect re-run on changes.
    const points = snapshots
      .map(snap => {
        const ch = snap.channels.find(c => c.id === channelId);
        if (!ch || ch.quality !== 'ok') return null;
        return { x: snap.timestamp, y: toDisplayTemp(ch.temperature_c, unit) };
      })
      .filter((p): p is { x: number; y: number } => p !== null);

    draw(el, points, color, unit, minDisplaySpan);
  });

  function draw(
    el:     HTMLCanvasElement,
    points: { x: number; y: number }[],
    stroke: string,
    unit:   TempUnit,
    minSpan: number,
  ): void {
    const dpr  = window.devicePixelRatio ?? 1;
    const rect = el.getBoundingClientRect();
    const w    = rect.width  || 300;
    const h    = rect.height || 56;

    el.width  = Math.round(w * dpr);
    el.height = Math.round(h * dpr);

    const ctx = el.getContext('2d');
    if (!ctx) return;
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, w, h);

    if (points.length < 2) {
      ctx.fillStyle    = 'rgba(255,255,255,0.06)';
      ctx.font         = `11px system-ui`;
      ctx.fillText('no data', 8, h / 2 + 4);
      return;
    }

    const xs   = points.map(p => p.x);
    const ys   = points.map(p => p.y);
    const xMin = Math.min(...xs), xMax = Math.max(...xs);
    const yMin = Math.min(...ys), yMax = Math.max(...ys);
    const span = yMax - yMin;
    const baseSpan = Math.max(span, minSpan);
    const yPad = Math.max(baseSpan * 0.12, 0.5);
    const yMid = (yMin + yMax) / 2;
    const plotMin = yMid - baseSpan / 2 - yPad;
    const plotMax = yMid + baseSpan / 2 + yPad;

    const px = (x: number) => ((x - xMin) / (xMax - xMin || 1)) * w;
    const py = (y: number) => h - ((y - plotMin) / (plotMax - plotMin || 1)) * h;

    // Filled area under the line.
    ctx.beginPath();
    ctx.moveTo(px(points[0].x), h);
    points.forEach(p => ctx.lineTo(px(p.x), py(p.y)));
    ctx.lineTo(px(points.at(-1)!.x), h);
    ctx.closePath();
    ctx.fillStyle = stroke + '22';
    ctx.fill();

    // Line.
    ctx.beginPath();
    points.forEach((p, i) => {
      if (i === 0) ctx.moveTo(px(p.x), py(p.y));
      else         ctx.lineTo(px(p.x), py(p.y));
    });
    ctx.strokeStyle = stroke;
    ctx.lineWidth   = 1.5;
    ctx.lineJoin    = 'round';
    ctx.lineCap     = 'round';
    ctx.stroke();

    // Show plotted bounds so compressed view has explicit context.
    const decimals = unit === 'F' ? 0 : 1;
    ctx.font = `10px var(--font-mono), ui-monospace, monospace`;
    ctx.textAlign = 'right';
    ctx.fillStyle = 'rgba(10, 13, 17, 0.66)';
    ctx.fillRect(w - 58, 2, 56, 12);
    ctx.fillRect(w - 58, h - 14, 56, 12);
    ctx.fillStyle = 'rgba(234, 237, 243, 0.9)';
    ctx.fillText(`${plotMax.toFixed(decimals)}°${unit}`, w - 4, 12);
    ctx.fillText(`${plotMin.toFixed(decimals)}°${unit}`, w - 4, h - 4);
  }
</script>

<canvas bind:this={canvas}></canvas>

<style>
  canvas {
    display: block;
    width: 100%;
    height: 56px;
    border-radius: var(--radius);
    background: var(--color-surface-alt);
  }
</style>
