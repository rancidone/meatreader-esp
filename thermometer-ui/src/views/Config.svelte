<script lang="ts">
  import { configStore } from '../lib/stores/config.svelte.ts';
  import type { ChannelConfig, AlertConfig, AlertMethod } from '../lib/api/types.ts';

  $effect(() => { void configStore.fetch(); });

  let localSsid        = $state('');
  let localPassword    = $state('');
  let localSampleRate  = $state(2.0);
  let localEmaAlpha    = $state(0.3);
  let localSpikeReject = $state(5.0);
  let localChannels    = $state<ChannelConfig[]>([]);
  let localAlerts      = $state<AlertConfig[]>([]);
  let showPassword     = $state(false);
  let dirty            = $state(false);

  function cloneChannels(chs: ChannelConfig[]): ChannelConfig[] {
    return chs.map(ch => ({ ...ch, steinhart_hart: { ...ch.steinhart_hart } }));
  }

  function cloneAlerts(alerts: AlertConfig[] | undefined, channelCount: number): AlertConfig[] {
    if (!alerts || alerts.length === 0) {
      return Array.from({ length: channelCount }, () => ({
        enabled: false,
        target_temp_c: 100,
        method: 'none' as AlertMethod,
        webhook_url: '',
      }));
    }
    return alerts.map(a => ({ ...a }));
  }

  $effect(() => {
    const cfg = configStore.status;
    if (!cfg) return;
    localSsid        = cfg.wifi_ssid;
    localPassword    = cfg.wifi_password;
    localSampleRate  = cfg.sample_rate_hz;
    localEmaAlpha    = cfg.ema_alpha;
    localSpikeReject = cfg.spike_reject_delta_c;
    localChannels    = cloneChannels(cfg.channels);
    localAlerts      = cloneAlerts(cfg.alerts, cfg.channels.length);
    dirty = false;
  });

  async function handleSave(): Promise<void> {
    await configStore.patch({
      wifi_ssid:            localSsid,
      wifi_password:        localPassword,
      sample_rate_hz:       localSampleRate,
      ema_alpha:            localEmaAlpha,
      spike_reject_delta_c: localSpikeReject,
      channels:             localChannels,
      alerts:               localAlerts,
    });
    dirty = false;
  }

  function fmtSH(a: number, b: number, c: number): string {
    return `${a.toExponential(3)}, ${b.toExponential(3)}, ${c.toExponential(3)}`;
  }
</script>

<div class="col">

  <div class="page-header">
    <h1>Config</h1>
    {#if configStore.error}
      <span class="badge error">{configStore.error}</span>
    {/if}
  </div>

  {#if configStore.loading && !configStore.status}
    <p class="muted">Loading…</p>
  {:else if configStore.status}

    <div class="actions card">
      <button
        class="primary"
        onclick={handleSave}
        disabled={configStore.loading}
      >Save</button>

      <button
        class="commit"
        onclick={() => void configStore.commit()}
        disabled={configStore.loading}
        title="Persist active config to flash"
      >Commit to flash</button>
    </div>

    <section class="card col">
      <fieldset>
        <legend>Wi-Fi</legend>
        <p class="hint warn-hint">Warning: saving new WiFi credentials will restart the device.</p>
        <div class="field-row">
          <div class="field">
            <label for="ssid">SSID</label>
            <input id="ssid" type="text" bind:value={localSsid} oninput={() => (dirty = true)} />
          </div>
          <div class="field">
            <label for="pw">Password</label>
            <div class="pw-row">
              <input
                id="pw"
                type={showPassword ? 'text' : 'password'}
                bind:value={localPassword}
                oninput={() => (dirty = true)}
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
                   bind:value={localSampleRate} oninput={() => (dirty = true)} />
          </div>
        </div>
        <div class="field-row">
          <div class="field">
            <label for="ema">Smoothing (EMA α) — {localEmaAlpha.toFixed(2)}</label>
            <input id="ema" type="range" step="0.01" min="0.01" max="1.0"
                   bind:value={localEmaAlpha} oninput={() => (dirty = true)} />
            <span class="hint">Lower = smoother but slower to respond</span>
          </div>
          <div class="field">
            <label for="spike">Spike rejection (°C) — {localSpikeReject.toFixed(1)}</label>
            <input id="spike" type="range" step="0.5" min="0.5" max="50"
                   bind:value={localSpikeReject} oninput={() => (dirty = true)} />
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
                     oninput={() => (dirty = true)}
                     placeholder="Channel {ch.adc_channel}" />
            </div>
            <div class="field checkbox-field">
              <label>
                <input type="checkbox" bind:checked={localChannels[i].enabled}
                       onchange={() => (dirty = true)} />
                Enabled
              </label>
            </div>
          </div>

          <details>
            <summary class="muted">Steinhart-Hart coefficients (set via Calibration)</summary>
            <div class="sh-readout">
              <span class="sh-label">A</span><span class="sh-val">{localChannels[i].steinhart_hart.a.toExponential(6)}</span>
              <span class="sh-label">B</span><span class="sh-val">{localChannels[i].steinhart_hart.b.toExponential(6)}</span>
              <span class="sh-label">C</span><span class="sh-val">{localChannels[i].steinhart_hart.c.toExponential(6)}</span>
            </div>
            <details class="sh-edit">
              <summary class="hint">Edit manually…</summary>
              <div class="field-row sh-fields">
                <div class="field">
                  <label for="sha{i}">A</label>
                  <input id="sha{i}" type="number" step="any"
                         bind:value={localChannels[i].steinhart_hart.a}
                         oninput={() => (dirty = true)} />
                </div>
                <div class="field">
                  <label for="shb{i}">B</label>
                  <input id="shb{i}" type="number" step="any"
                         bind:value={localChannels[i].steinhart_hart.b}
                         oninput={() => (dirty = true)} />
                </div>
                <div class="field">
                  <label for="shc{i}">C</label>
                  <input id="shc{i}" type="number" step="any"
                         bind:value={localChannels[i].steinhart_hart.c}
                         oninput={() => (dirty = true)} />
                </div>
              </div>
            </details>
          </details>
        </fieldset>
      {/each}

      {#each localAlerts as _alert, i}
        <fieldset>
          <legend>Channel {i} — Alert</legend>
          <div class="field-row">
            <div class="field checkbox-field">
              <label>
                <input type="checkbox" bind:checked={localAlerts[i].enabled}
                       onchange={() => (dirty = true)} />
                Enable alert
              </label>
            </div>
          </div>

          {#if localAlerts[i].enabled}
            <div class="field-row">
              <div class="field">
                <label for="alert-target-{i}">Target temperature (°C)</label>
                <input id="alert-target-{i}" type="number" step="0.5" min="0" max="300"
                       bind:value={localAlerts[i].target_temp_c}
                       oninput={() => (dirty = true)} />
              </div>
              <div class="field">
                <label for="alert-method-{i}">Alert method</label>
                <select id="alert-method-{i}" bind:value={localAlerts[i].method}
                        onchange={() => (dirty = true)}>
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
                         bind:value={localAlerts[i].webhook_url}
                         oninput={() => (dirty = true)} />
                  <span class="hint">Device will POST JSON to this URL when the alert fires.</span>
                </div>
              </div>
            {/if}
          {/if}
        </fieldset>
      {/each}
    </section>

    {#if dirty}
      <p class="hint warn-hint">Unsaved changes — click Save to apply.</p>
    {/if}

  {/if}
</div>

<style>
  .page-header {
    display: flex;
    align-items: center;
    gap: var(--gap-sm);
  }

  .actions {
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

  .pw-row input { flex: 1; }

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

  .sh-readout {
    display: grid;
    grid-template-columns: 1.2ch 1fr;
    gap: 0.15rem var(--gap-sm);
    margin-top: var(--gap-sm);
    font-family: var(--font-mono);
    font-size: 0.88rem;
  }

  .sh-label {
    color: var(--color-text-muted);
    font-weight: 600;
    align-self: center;
  }

  .sh-val {
    color: var(--color-text);
    letter-spacing: 0.02em;
  }

  .sh-edit { margin-top: var(--gap-sm); }

  .sh-edit > summary {
    cursor: pointer;
    user-select: none;
    padding: 0.15rem 0;
  }

  .sh-fields { margin-top: var(--gap-sm); }

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
