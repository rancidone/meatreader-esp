<script lang="ts">
  import { configStore } from '../lib/stores/config.svelte.ts';
  import type { ChannelConfig, AlertConfig, AlertMethod } from '../lib/api/types.ts';

  // Load config on mount.
  $effect(() => { void configStore.fetch(); });

  // ── Local staged form state ───────────────────────────────────────────
  // Initialised from staged; re-synced whenever staged changes after a
  // fetch/patch so that form always reflects the server's staged state.

  let localSsid          = $state('');
  let localPassword      = $state('');
  let localSampleRate    = $state(2.0);
  let localEmaAlpha      = $state(0.3);
  let localSpikeReject   = $state(5.0);
  let localChannels      = $state<ChannelConfig[]>([]);
  let localAlerts        = $state<AlertConfig[]>([]);
  let showPassword     = $state(false);

  // Deep-copy channels so mutations don't alias store objects.
  function cloneChannels(chs: ChannelConfig[]): ChannelConfig[] {
    return chs.map(ch => ({ ...ch, steinhart_hart: { ...ch.steinhart_hart } }));
  }

  function cloneAlerts(alerts: AlertConfig[] | undefined): AlertConfig[] {
    if (!alerts || alerts.length === 0) {
      // Default: two disabled alerts.
      return [0, 1].map(() => ({
        enabled: false,
        target_temp_c: 100,
        method: 'none' as AlertMethod,
        webhook_url: '',
      }));
    }
    return alerts.map(a => ({ ...a }));
  }

  $effect(() => {
    const staged = configStore.status?.staged;
    if (!staged) return;
    localSsid        = staged.wifi_ssid;
    localPassword    = staged.wifi_password;
    localSampleRate  = staged.sample_rate_hz;
    localEmaAlpha    = staged.ema_alpha;
    localSpikeReject = staged.spike_reject_delta_c;
    localChannels    = cloneChannels(staged.channels);
    localAlerts      = cloneAlerts(staged.alerts);
  });

  // ── Derived flags ─────────────────────────────────────────────────────

  const stagedVsActive = $derived(
    configStore.status
      ? JSON.stringify(configStore.status.staged) !== JSON.stringify(configStore.status.active)
      : false
  );

  const activeVsPersisted = $derived(
    configStore.status
      ? JSON.stringify(configStore.status.active) !== JSON.stringify(configStore.status.persisted)
      : false
  );

  // ── Actions ───────────────────────────────────────────────────────────

  async function saveToStaged(): Promise<void> {
    await configStore.patchStaged({
      wifi_ssid:            localSsid,
      wifi_password:        localPassword,
      sample_rate_hz:       localSampleRate,
      ema_alpha:            localEmaAlpha,
      spike_reject_delta_c: localSpikeReject,
      channels:             localChannels,
      alerts:               localAlerts,
    });
  }

  async function handleRevertActive(): Promise<void> {
    if (!confirm('Revert the active (running) config to the persisted (flash) values?')) return;
    await configStore.revertActive();
  }

  // ── Comparison helpers ────────────────────────────────────────────────

  function fmtSH(a: number, b: number, c: number): string {
    return `${a.toExponential(3)}, ${b.toExponential(3)}, ${c.toExponential(3)}`;
  }
</script>

<div class="col">

  <!-- ── Header ──────────────────────────────────────────────────────── -->
  <div class="page-header">
    <h1>Config</h1>
    {#if configStore.error}
      <span class="badge error">{configStore.error}</span>
    {/if}
  </div>

  <!-- ── State flow explainer ────────────────────────────────────────── -->
  <div class="flow card">
    <div class="flow-step">
      <div class="flow-label">Persisted</div>
      <div class="flow-desc muted">Saved to flash</div>
    </div>
    <span class="flow-arrow">→</span>
    <div class="flow-step">
      <div class="flow-label">Active</div>
      <div class="flow-desc muted">Currently running</div>
      {#if activeVsPersisted}
        <span class="badge warn">differs from flash</span>
      {/if}
    </div>
    <span class="flow-arrow">→</span>
    <div class="flow-step">
      <div class="flow-label">Staged</div>
      <div class="flow-desc muted">Pending edits</div>
      {#if stagedVsActive}
        <span class="badge warn">unsaved changes</span>
      {/if}
    </div>
  </div>

  <!-- ── Loading ─────────────────────────────────────────────────────── -->
  {#if configStore.loading && !configStore.status}
    <p class="muted">Loading…</p>
  {:else if configStore.status}

    <!-- ── Action bar ──────────────────────────────────────────────── -->
    <div class="actions card">
      <div class="actions-group">
        <button
          onclick={saveToStaged}
          disabled={configStore.loading}
          title="Send form edits to staged config"
        >Save to staged</button>

        <button
          class="primary"
          onclick={() => void configStore.apply()}
          disabled={configStore.loading || !stagedVsActive}
          title="staged → active (applies to running firmware)"
        >Apply</button>

        <button
          class="commit"
          onclick={() => void configStore.commit()}
          disabled={configStore.loading || !activeVsPersisted}
          title="active → flash (permanent write)"
        >Commit to flash</button>
      </div>

      <div class="actions-group">
        <button
          onclick={() => void configStore.revertStaged()}
          disabled={configStore.loading || !stagedVsActive}
          title="Discard staged edits — staged ← active"
        >Revert staged</button>

        <button
          class="danger"
          onclick={handleRevertActive}
          disabled={configStore.loading || !activeVsPersisted}
          title="active ← persisted (rolls back running config)"
        >Revert active</button>
      </div>
    </div>

    <!-- ── Staged edit form ────────────────────────────────────────── -->
    <section class="card col">
      <h2>Staged config <span class="muted">(editing)</span></h2>

      <fieldset>
        <legend>Wi-Fi</legend>
        <p class="hint warn-hint">Warning: saving new WiFi credentials will restart the device.</p>
        <div class="field-row">
          <div class="field">
            <label for="ssid">SSID</label>
            <input id="ssid" type="text" bind:value={localSsid} />
          </div>
          <div class="field">
            <label for="pw">Password</label>
            <div class="pw-row">
              <input
                id="pw"
                type={showPassword ? 'text' : 'password'}
                bind:value={localPassword}
              />
              <button
                class="icon-btn"
                onclick={() => (showPassword = !showPassword)}
                title={showPassword ? 'Hide' : 'Show'}
              >{showPassword ? '🙈' : '👁'}</button>
            </div>
          </div>
        </div>
      </fieldset>

      <fieldset>
        <legend>Sampling</legend>
        <div class="field-row">
          <div class="field">
            <label for="sr">Sample rate (Hz)</label>
            <input id="sr" type="number" step="0.5" min="0.1" max="10"
                   bind:value={localSampleRate} />
          </div>
        </div>
        <div class="field-row">
          <div class="field">
            <label for="ema">Smoothing (EMA α) — {localEmaAlpha.toFixed(2)}</label>
            <input id="ema" type="range" step="0.01" min="0.01" max="1.0"
                   bind:value={localEmaAlpha} />
            <span class="hint">Lower = smoother but slower to respond</span>
          </div>
          <div class="field">
            <label for="spike">Spike rejection (°C) — {localSpikeReject.toFixed(1)}</label>
            <input id="spike" type="range" step="0.5" min="0.5" max="50"
                   bind:value={localSpikeReject} />
            <span class="hint">Readings jumping more than this are ignored</span>
          </div>
        </div>
      </fieldset>

      {#each localChannels as ch, i}
        <fieldset>
          <legend>Channel {ch.adc_channel}</legend>
          <div class="field-row">
            <div class="field">
              <label for="label{i}">Label</label>
              <input id="label{i}" type="text" maxlength="31"
                     bind:value={localChannels[i].label}
                     placeholder="Channel {ch.adc_channel}" />
            </div>
            <div class="field checkbox-field">
              <label>
                <input type="checkbox" bind:checked={localChannels[i].enabled} />
                Enabled
              </label>
            </div>
          </div>

          <details>
            <summary class="muted">Steinhart-Hart coefficients (set via Calibration)</summary>
            <div class="field-row sh-fields">
              <div class="field">
                <label for="sha{i}">A</label>
                <input id="sha{i}" type="number" step="any"
                       bind:value={localChannels[i].steinhart_hart.a} />
              </div>
              <div class="field">
                <label for="shb{i}">B</label>
                <input id="shb{i}" type="number" step="any"
                       bind:value={localChannels[i].steinhart_hart.b} />
              </div>
              <div class="field">
                <label for="shc{i}">C</label>
                <input id="shc{i}" type="number" step="any"
                       bind:value={localChannels[i].steinhart_hart.c} />
              </div>
            </div>
          </details>
        </fieldset>
      {/each}

      {#each localAlerts as _alert, i}
        <fieldset>
          <legend>Channel {i} — Alert</legend>
          <div class="field-row">
            <div class="field checkbox-field">
              <label>
                <input type="checkbox" bind:checked={localAlerts[i].enabled} />
                Enable alert
              </label>
            </div>
          </div>

          {#if localAlerts[i].enabled}
            <div class="field-row">
              <div class="field">
                <label for="alert-target-{i}">Target temperature (°C)</label>
                <input id="alert-target-{i}" type="number" step="0.5" min="0" max="300"
                       bind:value={localAlerts[i].target_temp_c} />
              </div>
              <div class="field">
                <label for="alert-method-{i}">Alert method</label>
                <select id="alert-method-{i}" bind:value={localAlerts[i].method}>
                  <option value="none">None</option>
                  <option value="webhook">Webhook (HTTP POST)</option>
                  <option value="mqtt">MQTT</option>
                </select>
              </div>
            </div>

            {#if localAlerts[i].method === 'webhook'}
              <div class="field-row">
                <div class="field">
                  <label for="alert-url-{i}">Webhook URL</label>
                  <input id="alert-url-{i}" type="url"
                         placeholder="http://192.168.1.x/notify"
                         bind:value={localAlerts[i].webhook_url} />
                  <span class="hint">Device will POST JSON to this URL when the alert fires.</span>
                </div>
              </div>
            {/if}
          {/if}
        </fieldset>
      {/each}
    </section>

    <!-- ── Comparison: active & persisted (read-only) ──────────────── -->
    <details class="card">
      <summary><strong>Active &amp; Persisted</strong> <span class="muted">(read-only comparison)</span></summary>

      {#if configStore.status}
        {@const p = configStore.status.persisted}
        {@const a = configStore.status.active}
        <table class="compare-table">
          <thead>
            <tr>
              <th>Field</th>
              <th>Persisted <span class="muted">(flash)</span></th>
              <th>Active <span class="muted">(running)</span></th>
            </tr>
          </thead>
          <tbody>
            <tr class:differs={p.wifi_ssid !== a.wifi_ssid}>
              <td>SSID</td>
              <td>{p.wifi_ssid || '—'}</td>
              <td>{a.wifi_ssid || '—'}</td>
            </tr>
            <tr class:differs={p.sample_rate_hz !== a.sample_rate_hz}>
              <td>Sample rate</td>
              <td>{p.sample_rate_hz} Hz</td>
              <td>{a.sample_rate_hz} Hz</td>
            </tr>
            <tr class:differs={p.ema_alpha !== a.ema_alpha}>
              <td>EMA α</td>
              <td>{p.ema_alpha}</td>
              <td>{a.ema_alpha}</td>
            </tr>
            <tr class:differs={p.spike_reject_delta_c !== a.spike_reject_delta_c}>
              <td>Spike reject (°C)</td>
              <td>{p.spike_reject_delta_c}</td>
              <td>{a.spike_reject_delta_c}</td>
            </tr>

            {#each p.channels as pch, i}
              {@const ach = a.channels[i]}
              <tr class:differs={pch.enabled !== ach?.enabled}>
                <td>Ch {pch.adc_channel} enabled</td>
                <td>{pch.enabled}</td>
                <td>{ach?.enabled ?? '—'}</td>
              </tr>
              <tr class:differs={JSON.stringify(pch.steinhart_hart) !== JSON.stringify(ach?.steinhart_hart)}>
                <td>Ch {pch.adc_channel} S-H</td>
                <td class="mono">{fmtSH(pch.steinhart_hart.a, pch.steinhart_hart.b, pch.steinhart_hart.c)}</td>
                <td class="mono">{ach ? fmtSH(ach.steinhart_hart.a, ach.steinhart_hart.b, ach.steinhart_hart.c) : '—'}</td>
              </tr>
            {/each}
          </tbody>
        </table>
      {/if}
    </details>

  {/if}
</div>

<style>
  .page-header {
    display: flex;
    align-items: center;
    gap: var(--gap-sm);
  }

  /* ── State flow ─────────────────────────────────────────────────── */

  .flow {
    display: flex;
    align-items: center;
    gap: var(--gap-sm);
    flex-wrap: wrap;
  }

  .flow-step {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .flow-label {
    font-weight: 600;
    font-size: 0.9rem;
  }

  .flow-desc {
    font-size: 0.75rem;
  }

  .flow-arrow {
    color: var(--color-text-muted);
    font-size: 1.2rem;
  }

  /* ── Actions bar ────────────────────────────────────────────────── */

  .actions {
    display: flex;
    align-items: center;
    justify-content: space-between;
    flex-wrap: wrap;
    gap: var(--gap-sm);
  }

  .actions-group {
    display: flex;
    gap: var(--gap-sm);
    flex-wrap: wrap;
  }

  button.commit {
    background: color-mix(in srgb, var(--color-warn) 20%, var(--color-surface-alt));
    border-color: var(--color-warn);
    color: var(--color-warn);
  }

  button.commit:hover:not(:disabled) {
    background: color-mix(in srgb, var(--color-warn) 30%, var(--color-surface-alt));
  }

  button.danger {
    background: color-mix(in srgb, var(--color-error) 15%, var(--color-surface-alt));
    border-color: var(--color-error);
    color: var(--color-error);
  }

  button.danger:hover:not(:disabled) {
    background: color-mix(in srgb, var(--color-error) 25%, var(--color-surface-alt));
  }

  /* ── Form ───────────────────────────────────────────────────────── */

  fieldset {
    border: 1px solid var(--color-border);
    border-radius: var(--radius);
    padding: var(--gap-sm) var(--gap);
    display: flex;
    flex-direction: column;
    gap: var(--gap-sm);
  }

  legend {
    padding: 0 0.4rem;
    font-size: 0.8rem;
    font-weight: 600;
    color: var(--color-text-muted);
    text-transform: uppercase;
    letter-spacing: 0.05em;
  }

  .field-row {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
    gap: var(--gap-sm);
  }

  .field {
    display: flex;
    flex-direction: column;
    gap: var(--gap-xs);
  }

  .hint {
    font-size: 0.72rem;
    color: var(--color-text-muted);
  }

  .warn-hint {
    color: var(--color-warn);
    margin: 0;
  }

  .checkbox-field label {
    display: flex;
    align-items: center;
    gap: var(--gap-xs);
    font-size: 0.9rem;
    color: var(--color-text);
    cursor: pointer;
    margin: 0;
  }

  .checkbox-field input[type="checkbox"] {
    width: auto;
  }

  .pw-row {
    display: flex;
    gap: var(--gap-xs);
  }

  .pw-row input {
    flex: 1;
  }

  .icon-btn {
    padding: 0.4rem 0.6rem;
    font-size: 0.9rem;
    flex-shrink: 0;
  }

  details summary {
    cursor: pointer;
    user-select: none;
    padding: 0.2rem 0;
  }

  .sh-fields {
    margin-top: var(--gap-sm);
  }

  /* ── Comparison table ───────────────────────────────────────────── */

  .compare-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.85rem;
    margin-top: var(--gap-sm);
  }

  .compare-table th,
  .compare-table td {
    text-align: left;
    padding: 0.4rem 0.6rem;
    border-bottom: 1px solid var(--color-border);
  }

  .compare-table th {
    color: var(--color-text-muted);
    font-weight: 600;
    font-size: 0.75rem;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  .compare-table tr.differs td {
    background: color-mix(in srgb, var(--color-warn) 8%, transparent);
  }

  .compare-table tr.differs td:first-child {
    color: var(--color-warn);
  }

  .mono {
    font-family: var(--font-mono);
    font-size: 0.78rem;
  }

  details[open] summary {
    margin-bottom: var(--gap-sm);
  }

  select {
    background: var(--color-surface-alt);
    border: 1px solid var(--color-border);
    border-radius: var(--radius);
    color: var(--color-text);
    padding: 0.4rem 0.5rem;
    font-size: 0.9rem;
    width: 100%;
  }
</style>
