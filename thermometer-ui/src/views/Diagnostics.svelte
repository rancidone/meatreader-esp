<script lang="ts">
  import { deviceStore } from '../lib/stores/device.svelte.ts';
  import { ui }          from '../lib/stores/ui.svelte.ts';
  import { formatUptime } from '../lib/utils/format.ts';

  $effect(() => {
    if (ui.polling) {
      deviceStore.start(10_000);
      return () => deviceStore.stop();
    } else {
      deviceStore.stop();
    }
  });
</script>

<div class="col">

  <!-- ── Header ───────────────────────────────────────────────────────── -->
  <div class="diag-header">
    <h2>Diagnostics</h2>
    <button onclick={() => deviceStore.refresh()} disabled={!deviceStore.connected}>
      Refresh
    </button>
  </div>

  <!-- ── Connection error ─────────────────────────────────────────────── -->
  {#if !deviceStore.connected}
    <div class="error-banner">
      {deviceStore.error ?? 'Device unreachable'}
    </div>
  {/if}

  <!-- ── Device info ──────────────────────────────────────────────────── -->
  <div class="card">
    <h3>Device</h3>
    <table class="info-table">
      <tbody>
        <tr><th>Platform</th><td>{deviceStore.deviceInfo?.platform ?? '—'}</td></tr>
        <tr><th>Firmware</th><td>{deviceStore.deviceInfo?.firmware ?? '—'}</td></tr>
        <tr><th>Channels</th><td>{deviceStore.deviceInfo?.channels ?? '—'}</td></tr>
      </tbody>
    </table>
  </div>

  <!-- ── Status ───────────────────────────────────────────────────────── -->
  <div class="card">
    <h3>Status</h3>
    {#if deviceStore.statusData}
      {@const s = deviceStore.statusData}
      <table class="info-table">
        <tbody>
          <tr>
            <th>Health</th>
            <td>
              <span class="badge {s.healthy ? 'ok' : 'error'}">
                {s.healthy ? 'healthy' : 'unhealthy'}
              </span>
            </td>
          </tr>
          <tr><th>Uptime</th><td>{formatUptime(s.uptime_ms)}</td></tr>
          <tr><th>Status</th><td>{s.status}</td></tr>
        </tbody>
      </table>

      {#if s.health_flags && s.health_flags.length > 0}
        <div class="flags">
          <span class="label">Flags</span>
          <div class="flag-list">
            {#each s.health_flags as flag}
              <span class="badge warn">{flag}</span>
            {/each}
          </div>
        </div>
      {/if}
    {:else}
      <p class="muted">Waiting for status data…</p>
    {/if}
  </div>

</div>

<style>
  .diag-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .diag-header h2 { margin: 0; }

  .info-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.9rem;
  }

  .info-table th {
    text-align: left;
    color: var(--color-text-muted);
    font-weight: 400;
    padding: 0.3rem 0.6rem 0.3rem 0;
    width: 35%;
    white-space: nowrap;
  }

  .info-table td {
    padding: 0.3rem 0;
    font-family: var(--font-mono);
  }

  .info-table tr + tr th,
  .info-table tr + tr td {
    border-top: 1px solid var(--color-border);
  }

  .flags {
    margin-top: var(--gap-sm);
    display: flex;
    align-items: flex-start;
    gap: var(--gap-sm);
  }

  .label {
    font-size: 0.8rem;
    color: var(--color-text-muted);
    padding-top: 0.15rem;
    white-space: nowrap;
  }

  .flag-list {
    display: flex;
    flex-wrap: wrap;
    gap: var(--gap-xs);
  }

  .error-banner {
    background: color-mix(in srgb, var(--color-error) 12%, transparent);
    border: 1px solid color-mix(in srgb, var(--color-error) 30%, transparent);
    color: var(--color-error);
    border-radius: var(--radius);
    padding: 0.5rem 0.85rem;
    font-size: 0.85rem;
  }
</style>
