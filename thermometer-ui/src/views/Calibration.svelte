<script lang="ts">
  import { calibrationStore } from '../lib/stores/calibration.svelte.ts';
  import { configStore }      from '../lib/stores/config.svelte.ts';
  import { calibration as calibrationApi } from '../lib/api/live.ts';
  import type { CalibrationLiveResponse }  from '../lib/api/live.ts';
  import { formatTemp } from '../lib/utils/format.ts';
  import { ui }         from '../lib/stores/ui.svelte.ts';

  let selectedChannel = $state(0);
  let liveReading     = $state<CalibrationLiveResponse | null>(null);
  let liveError       = $state<string | null>(null);

  // Reset error when switching channels so stale messages don't linger.
  $effect(() => {
    void selectedChannel;
    calibrationStore.error = null;
    liveReading = null;
    liveError   = null;
  });

  // Poll /calibration/live every 2 s while a session is active for this channel.
  $effect(() => {
    if (!sessionIsForChannel || !ui.polling) return;

    let timer: ReturnType<typeof setInterval> | null = null;

    async function poll(): Promise<void> {
      try {
        liveReading = await calibrationApi.live(selectedChannel);
        liveError   = null;
      } catch (e) {
        liveError = e instanceof Error ? e.message : String(e);
      }
    }

    void poll();
    timer = setInterval(() => void poll(), 2000);
    return () => { if (timer !== null) clearInterval(timer); };
  });

  const MIN_POINTS = 3;

  const canSolve = $derived(
    calibrationStore.session !== null &&
    calibrationStore.session.channel === selectedChannel &&
    calibrationStore.session.points.length >= MIN_POINTS
  );

  const sessionIsForChannel = $derived(
    calibrationStore.session?.channel === selectedChannel
  );

  async function handleStartSession(): Promise<void> {
    if (
      calibrationStore.session &&
      calibrationStore.session.channel !== selectedChannel
    ) {
      if (!confirm(`Reset the existing session for channel ${calibrationStore.session.channel} and start a new one for channel ${selectedChannel}?`)) return;
    }
    await calibrationStore.startSession(selectedChannel);
  }

  async function handleAccept(): Promise<void> {
    await calibrationStore.accept(selectedChannel);
    // Refresh config so staged reflects the new coefficients.
    await configStore.fetch();
  }

  function fmtSH(v: number): string {
    return v.toExponential(6);
  }
</script>

<div class="col">
  <h1>Calibration</h1>
  <p class="muted">
    Fit Steinhart-Hart coefficients for a thermistor channel using a DS18B20 reference sensor.
    Minimum {MIN_POINTS} capture points required.
  </p>

  <!-- ── Error banner ─────────────────────────────────────────────────── -->
  {#if calibrationStore.error}
    <div class="error-banner">{calibrationStore.error}</div>
  {/if}

  <!-- ── Channel selector ─────────────────────────────────────────────── -->
  <div class="card row channel-bar">
    <span class="field-label">Channel</span>
    <div class="channel-group">
      {#each [0, 1] as ch}
        <button
          class="ch-btn"
          class:active={selectedChannel === ch}
          onclick={() => (selectedChannel = ch)}
          disabled={calibrationStore.loading}
        >Ch {ch}</button>
      {/each}
    </div>

    {#if calibrationStore.session && !sessionIsForChannel}
      <span class="badge warn">session active for ch {calibrationStore.session.channel}</span>
    {/if}
  </div>

  <!-- ── Session panel ─────────────────────────────────────────────────── -->
  <section class="card col">
    <div class="section-header">
      <h2>Session</h2>
      {#if calibrationStore.session && sessionIsForChannel}
        <span class="badge ok">active</span>
      {:else}
        <span class="badge disabled">none</span>
      {/if}
    </div>

    {#if calibrationStore.session && sessionIsForChannel}
      <!-- Points list -->
      <div class="points-section">
        <div class="points-header">
          <span class="muted">
            {calibrationStore.session.points.length} point{calibrationStore.session.points.length === 1 ? '' : 's'}
            captured
            {#if calibrationStore.session.points.length < MIN_POINTS}
              — need {MIN_POINTS - calibrationStore.session.points.length} more
            {/if}
          </span>
        </div>

        {#if calibrationStore.session.points.length > 0}
          <table class="points-table">
            <thead>
              <tr>
                <th>#</th>
                <th>Reference temp (DS18B20)</th>
                <th>Raw ADC</th>
              </tr>
            </thead>
            <tbody>
              {#each calibrationStore.session.points as pt, i}
                <tr>
                  <td class="mono">{i + 1}</td>
                  <td class="mono">{pt.reference_temp_c.toFixed(3)} °C</td>
                  <td class="mono">{pt.raw_adc}</td>
                </tr>
              {/each}
            </tbody>
          </table>
        {:else}
          <p class="muted empty-note">No points yet. Capture at least {MIN_POINTS} at different temperatures.</p>
        {/if}
      </div>

      <!-- ── Live reading ───────────────────────────────────────────── -->
      <div class="live-panel">
        {#if liveError}
          <span class="muted live-err">{liveError}</span>
        {:else if liveReading}
          <div class="live-col">
            <span class="live-label">DS18B20 reference</span>
            <span class="live-temp {liveReading.reference_temp_c === null ? 'unavail' : ''}">
              {liveReading.reference_temp_c !== null
                ? formatTemp(liveReading.reference_temp_c, ui.unit)
                : '— (not wired)'}
            </span>
          </div>
          <div class="live-divider"></div>
          <div class="live-col">
            <span class="live-label">Thermistor Ch {selectedChannel}</span>
            <span class="live-temp {liveReading.quality !== 'ok' ? 'unavail' : ''}">
              {liveReading.thermistor_temp_c !== null && liveReading.quality === 'ok'
                ? formatTemp(liveReading.thermistor_temp_c, ui.unit)
                : `— (${liveReading.quality})`}
            </span>
            <span class="live-adc">ADC {liveReading.raw_adc.toFixed(0)}</span>
          </div>
        {:else}
          <span class="muted">Waiting for live reading…</span>
        {/if}
      </div>

      <!-- Capture + Solve + Reset -->
      <div class="session-actions">
        <button
          onclick={() => void calibrationStore.capturePoint(selectedChannel)}
          disabled={calibrationStore.loading}
          title="Read DS18B20 + ADS1115 and record a calibration point"
        >Capture point</button>

        <button
          class="primary"
          onclick={() => void calibrationStore.solve(selectedChannel)}
          disabled={calibrationStore.loading || !canSolve}
          title={canSolve ? 'Compute Steinhart-Hart fit' : `Need at least ${MIN_POINTS} points`}
        >Solve</button>

        <button
          onclick={() => calibrationStore.reset()}
          disabled={calibrationStore.loading}
        >Reset session</button>
      </div>

    {:else}
      <p class="muted">Start a session to begin collecting calibration points.</p>
      <div>
        <button
          class="primary"
          onclick={handleStartSession}
          disabled={calibrationStore.loading}
        >Start session for Ch {selectedChannel}</button>
      </div>
    {/if}
  </section>

  <!-- ── Pending coefficients ──────────────────────────────────────────── -->
  {#if calibrationStore.pendingCoefficients && sessionIsForChannel}
    <section class="card col coeffs-card">
      <div class="section-header">
        <h2>Solved coefficients</h2>
        <span class="badge ok">ready to accept</span>
      </div>

      <table class="coeffs-table">
        <tbody>
          <tr>
            <td class="coeff-label">A</td>
            <td class="mono">{fmtSH(calibrationStore.pendingCoefficients.a)}</td>
          </tr>
          <tr>
            <td class="coeff-label">B</td>
            <td class="mono">{fmtSH(calibrationStore.pendingCoefficients.b)}</td>
          </tr>
          <tr>
            <td class="coeff-label">C</td>
            <td class="mono">{fmtSH(calibrationStore.pendingCoefficients.c)}</td>
          </tr>
        </tbody>
      </table>

      <div class="accept-row">
        <button
          class="primary"
          onclick={handleAccept}
          disabled={calibrationStore.loading}
        >Accept — write to staged config</button>
        <span class="muted accept-hint">
          Then go to <strong>Config</strong> → Apply → Commit to make it permanent.
        </span>
      </div>
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

  /* ── Channel bar ──────────────────────────────────────────────────── */

  .channel-bar {
    align-items: center;
    gap: var(--gap-sm);
    flex-wrap: wrap;
  }

  .field-label {
    font-size: 0.85rem;
    color: var(--color-text-muted);
  }

  .channel-group {
    display: flex;
    gap: 4px;
  }

  .ch-btn {
    padding: 0.3rem 0.8rem;
    font-size: 0.85rem;
    font-family: var(--font-mono);
  }

  .ch-btn.active {
    background: var(--color-accent);
    border-color: var(--color-accent);
    color: #fff;
  }

  /* ── Section header ───────────────────────────────────────────────── */

  .section-header {
    display: flex;
    align-items: center;
    gap: var(--gap-sm);
  }

  /* ── Points ───────────────────────────────────────────────────────── */

  .points-section {
    display: flex;
    flex-direction: column;
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

  /* ── Session actions ──────────────────────────────────────────────── */

  .session-actions {
    display: flex;
    gap: var(--gap-sm);
    flex-wrap: wrap;
  }

  /* ── Live panel ───────────────────────────────────────────────────── */

  .live-panel {
    display: flex;
    align-items: center;
    gap: var(--gap);
    background: var(--color-surface-alt);
    border-radius: var(--radius);
    padding: 0.75rem 1rem;
    min-height: 4.5rem;
  }

  .live-col {
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

  .live-err { font-size: 0.82rem; }

  /* ── Coefficients ─────────────────────────────────────────────────── */

  .coeffs-card { border-color: var(--color-ok); }

  .coeffs-table { border-collapse: collapse; font-size: 0.9rem; }

  .coeffs-table td { padding: 0.3rem 0.8rem 0.3rem 0; }

  .coeff-label {
    color: var(--color-text-muted);
    font-weight: 600;
    width: 2rem;
  }

  .accept-row {
    display: flex;
    align-items: center;
    gap: var(--gap-sm);
    flex-wrap: wrap;
  }

  .accept-hint { font-size: 0.82rem; }

  .mono { font-family: var(--font-mono); }
</style>
