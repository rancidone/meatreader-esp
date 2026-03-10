<script lang="ts">
  import Dashboard    from './views/Dashboard.svelte';
  import Config       from './views/Config.svelte';
  import Calibration  from './views/Calibration.svelte';
  import Diagnostics  from './views/Diagnostics.svelte';
  import Firmware     from './views/Firmware.svelte';

  type View = 'dashboard' | 'config' | 'calibration' | 'diagnostics' | 'firmware';

  let active = $state<View>('dashboard');

  const tabs: { id: View; label: string }[] = [
    { id: 'dashboard',   label: 'Dashboard'   },
    { id: 'config',      label: 'Config'      },
    { id: 'calibration', label: 'Calibration' },
    { id: 'diagnostics', label: 'Diagnostics' },
    { id: 'firmware',    label: 'Firmware'    },
  ];
</script>

<div class="shell">
  <header class="topbar">
    <span class="wordmark">Meatreader</span>
    <nav class="tabs">
      {#each tabs as tab}
        <button
          class="tab"
          class:active={active === tab.id}
          onclick={() => (active = tab.id)}
        >{tab.label}</button>
      {/each}
    </nav>
  </header>

  <main class="content">
    {#if active === 'dashboard'}
      <Dashboard />
    {:else if active === 'config'}
      <Config />
    {:else if active === 'calibration'}
      <Calibration />
    {:else if active === 'diagnostics'}
      <Diagnostics />
    {:else if active === 'firmware'}
      <Firmware />
    {/if}
  </main>
</div>

<style>
  .shell {
    display: flex;
    flex-direction: column;
    min-height: 100dvh;
  }

  /* ── Topbar ─────────────────────────────────────────────────────────── */

  .topbar {
    display: flex;
    align-items: center;
    gap: 2rem;
    padding: 0 1.25rem;
    height: 52px;
    background: var(--color-surface);
    border-bottom: 1px solid var(--color-border);
    position: sticky;
    top: 0;
    z-index: 10;
  }

  .wordmark {
    font-weight: 700;
    font-size: 1rem;
    color: var(--color-accent);
    letter-spacing: -0.02em;
    white-space: nowrap;
  }

  /* ── Tabs ───────────────────────────────────────────────────────────── */

  .tabs {
    display: flex;
    gap: 0.25rem;
  }

  .tab {
    background: transparent;
    border: none;
    border-radius: var(--radius);
    color: var(--color-text-muted);
    font-size: 0.875rem;
    font-weight: 500;
    padding: 0.35rem 0.85rem;
  }

  .tab:hover {
    background: var(--color-surface-alt);
    color: var(--color-text);
  }

  .tab.active {
    background: var(--color-surface-alt);
    color: var(--color-text);
  }

  /* ── Content ────────────────────────────────────────────────────────── */

  .content {
    flex: 1;
    padding: 1.5rem 1.25rem;
    max-width: 900px;
    width: 100%;
    margin: 0 auto;
  }
</style>
