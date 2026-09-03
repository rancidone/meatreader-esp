<script lang="ts">
  import { calibrationStore } from '../lib/stores/calibration.svelte.ts';
  import { configStore }      from '../lib/stores/config.svelte.ts';
  import { calibration as calibrationApi } from '../lib/api/live.ts';
  import type { CalibrationLiveResponse }  from '../lib/api/live.ts';
  import { formatTemp } from '../lib/utils/format.ts';
  import { ui }         from '../lib/stores/ui.svelte.ts';

  let liveReading = $state<CalibrationLiveResponse | null>(null);
  let liveError   = $state<string | null>(null);

  const MIN_POINTS = 3;

  const hasSession = $derived(calibrationStore.sessions.length > 0);
  const pointCount = $derived(calibrationStore.sessions[0]?.points.length ?? 0);
  const canAccept  = $derived(hasSession && pointCount >= MIN_POINTS);

  // Poll /calibration/live every 2 s while a session is active.
  $effect(() => {
    if (!hasSession || !ui.polling) return;

    let timer: ReturnType<typeof setInterval> | null = null;

    async function poll(): Promise<void> {
      try {
        liveReading = await calibrationApi.live();
        liveError   = null;
      } catch (e) {
        liveError = e instanceof Error ? e.message : String(e);
      }
    }

    void poll();
    timer = setInterval(() => void poll(), 2000);
    return () => { if (timer !== null) clearInterval(timer); };
  });

  async function handleAccept(): Promise<void> {
    await calibrationStore.accept();
    await configStore.fetch();
  }

  function fmtSH(v: number): string {
    return v.toExponential(6);
  }
</script>

<div class="col">
  <h1>Calibration</h1>
  <p class="muted">
    Fit Steinhart-Hart coefficients for all channels using a DS18B20 reference sensor.
    Minimum {MIN_POINTS} capture points required. Each capture reads the DS18B20 once and records all channels simultaneously.
  </p>

  {#if calibrationStore.error}
    <div class="error-banner">{calibrationStore.error}</div>
  {/if}

  <!-- ── Session panel ─────────────────────────────────────────────────── -->
  <section class="card col">
    <div class="section-header">
      <h2>Session</h2>
      {#if hasSession}
        <span class="badge ok">active — {pointCount} point{pointCount === 1 ? '' : 's'}</span>
      {:else}
        <span class="badge disabled">none</span>
      {/if}
    </div>

    {#if hasSession}
      <!-- Points table — show first channel's points (reference temp is shared) -->
      {#if calibrationStore.sessions[0]?.points.length > 0}
        <table class="points-table">
          <thead>
            <tr>
              <th>#</th>
              <th>Reference (DS18B20)</th>
              {#each calibrationStore.sessions as s}
                <th>Ch {s.channel} ADC</th>
              {/each}
            </tr>
          </thead>
          <tbody>
            {#each calibrationStore.sessions[0].points as pt, i}
              <tr>
                <td class="mono">{i + 1}</td>
                <td class="mono">{pt.reference_temp_c.toFixed(2)} °C</td>
                {#each calibrationStore.sessions as s}
                  <td class="mono">{s.points[i]?.raw_adc ?? '—'}</td>
                {/each}
              </tr>
            {/each}
          </tbody>
        </table>
      {:else}
        <p class="muted empty-note">No points yet. Place probes in a stable temperature bath and capture.</p>
      {/if}

      <!-- Live readings -->
      <div class="live-panel">
        {#if liveError}
          <span class="muted">{liveError}</span>
        {:else if liveReading}
          <div class="live-ref">
            <span class="live-label">DS18B20 reference</span>
            <span class="live-temp {liveReading.reference_temp_c === null ? 'unavail' : ''}">
              {liveReading.reference_temp_c !== null
                ? formatTemp(liveReading.reference_temp_c, ui.unit)
                : '— (not wired)'}
            </span>
          </div>
          <div class="live-divider"></div>
          {#each liveReading.channels as ch}
            <div class="live-col">
              <span class="live-label">Ch {ch.channel}</span>
              <span class="live-temp {ch.quality !== 'ok' ? 'unavail' : ''}">
                {ch.thermistor_temp_c !== null && ch.quality === 'ok'
                  ? formatTemp(ch.thermistor_temp_c, ui.unit)
                  : `— (${ch.quality})`}
              </span>
              {#if ch.temp_default_c !== null && ch.quality === 'ok'}
                <span class="live-default">
                  default: {formatTemp(ch.temp_default_c, ui.unit)}
                </span>
              {/if}
              <span class="live-adc">ADC {ch.raw_adc?.toFixed(0) ?? '—'}</span>
            </div>
          {/each}
        {:else}
          <span class="muted">Waiting for live reading…</span>
        {/if}
      </div>

      <div class="session-actions">
        <button
          onclick={() => void calibrationStore.capturePoint()}
          disabled={calibrationStore.loading}
        >Capture point</button>

        <button
          class="primary"
          onclick={handleAccept}
          disabled={calibrationStore.loading || !canAccept}
          title={canAccept ? 'Solve and write coefficients to active config' : `Need at least ${MIN_POINTS} points`}
        >Accept calibration</button>

        <button
          onclick={() => calibrationStore.reset()}
          disabled={calibrationStore.loading}
        >Reset</button>
      </div>

    {:else}
      <p class="muted">Start a session to begin collecting calibration points.</p>
      <div>
        <button
          class="primary"
          onclick={() => void calibrationStore.startSession()}
          disabled={calibrationStore.loading}
        >Start session</button>
      </div>
    {/if}
  </section>

  <!-- ── Results ───────────────────────────────────────────────────────── -->
  {#if calibrationStore.pendingResults.length > 0}
    <section class="card col coeffs-card">
      <div class="section-header">
        <h2>Calibration results</h2>
        {#if calibrationStore.pendingResults.every(r => r.status === 'ok')}
          <span class="badge ok">written to config</span>
        {:else}
          <span class="badge error">some channels failed</span>
        {/if}
      </div>

      {#each calibrationStore.pendingResults as result}
        <div class="result-row">
          <span class="result-ch">Ch {result.channel}</span>
          {#if result.status === 'ok' && result.coefficients}
            <span class="badge ok">ok</span>
            <span class="mono coeff-vals">
              A={fmtSH(result.coefficients.a)}
              B={fmtSH(result.coefficients.b)}
              C={fmtSH(result.coefficients.c)}
            </span>
          {:else}
            <span class="badge error">failed</span>
            <span class="muted">{result.error ?? 'solver error'}</span>
          {/if}
        </div>
      {/each}

      <p class="hint">
        Go to <strong>Config → Commit to flash</strong> to make this permanent.
      </p>
    </section>
  {/if}
</div>

<style>
  .error-banner {
    background: color-mix(in srgb, var(--color-error) 12%, transparent);
    border: 1px solid color-mix(in srgb, var(--color-error) 30%, transparent);
    color: var(--color-error);
    border-radius: var(--radius);
    padding: 0.5rem 0.85rem;
    font-size: 0.85rem;
  }

  .section-header {
    display: flex;
    align-items: center;
    gap: var(--gap-sm);
  }

  .points-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.85rem;
  }

  .points-table th,
  .points-table td {
    text-align: left;
    padding: 0.35rem 0.6rem;
    border-bottom: 1px solid var(--color-border);
  }

  .points-table th {
    color: var(--color-text-muted);
    font-size: 0.75rem;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  .empty-note { font-size: 0.85rem; padding: 0.5rem 0; }

  .session-actions {
    display: flex;
    gap: var(--gap-sm);
    flex-wrap: wrap;
  }

  .live-panel {
    display: flex;
    align-items: center;
    gap: var(--gap);
    background: var(--color-surface-alt);
    border-radius: var(--radius);
    padding: 0.75rem 1rem;
    min-height: 4.5rem;
    flex-wrap: wrap;
  }

  .live-ref, .live-col {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
    flex: 1;
  }

  .live-label {
    font-size: 0.7rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--color-text-muted);
  }

  .live-temp {
    font-size: 1.6rem;
    font-weight: 700;
    font-family: var(--font-mono);
    letter-spacing: -0.02em;
    line-height: 1.1;
    color: var(--color-text);
  }

  .live-temp.unavail {
    color: var(--color-text-muted);
    font-size: 1rem;
    font-weight: 400;
  }

  .live-default {
    font-size: 0.75rem;
    font-family: var(--font-mono);
    color: var(--color-text-muted);
  }

  .live-adc {
    font-size: 0.75rem;
    font-family: var(--font-mono);
    color: var(--color-text-muted);
  }

  .live-divider {
    width: 1px;
    align-self: stretch;
    background: var(--color-border);
  }

  .coeffs-card { border-color: var(--color-ok); }

  .result-row {
    display: flex;
    align-items: center;
    gap: var(--gap-sm);
    flex-wrap: wrap;
  }

  .result-ch {
    font-family: var(--font-mono);
    font-weight: 600;
    min-width: 3rem;
  }

  .coeff-vals {
    font-size: 0.8rem;
    color: var(--color-text-muted);
  }

  .hint {
    font-size: 0.82rem;
    color: var(--color-text-muted);
  }

  .mono { font-family: var(--font-mono); }
</style>
