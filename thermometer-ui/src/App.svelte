<script lang="ts">
  import Dashboard    from './views/Dashboard.svelte';
  import Config       from './views/Config.svelte';
  import Calibration  from './views/Calibration.svelte';
  import Diagnostics  from './views/Diagnostics.svelte';
  import Profiles     from './views/Profiles.svelte';
  import Firmware     from './views/Firmware.svelte';
  import { tempsStore } from './lib/stores/temps.svelte.ts';
  import { postSnapshotToSW } from './lib/sw-bridge.ts';

  type View = 'dashboard' | 'config' | 'calibration' | 'diagnostics' | 'profiles' | 'firmware';

  let active = $state<View>('dashboard');

  // Forward snapshots to service worker for background notifications
  $effect(() => {
    const snap = tempsStore.latest;
    if (snap) void postSnapshotToSW(snap);
  });

  // PWA install prompt
  let installPrompt = $state<Event & { prompt(): Promise<void> } | null>(null);

  if (typeof window !== 'undefined') {
    window.addEventListener('beforeinstallprompt', (e) => {
      e.preventDefault();
      installPrompt = e as Event & { prompt(): Promise<void> };
    });
    window.addEventListener('appinstalled', () => {
      installPrompt = null;
    });
  }

  const tabs: { id: View; label: string }[] = [
    { id: 'dashboard',   label: 'Dashboard'   },
    { id: 'profiles',    label: 'Profiles'    },
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

  {#if tempsStore.error}
    <div class="offline-banner" role="alert">
      <span>Device offline — {tempsStore.error}</span>
    </div>
  {/if}

  {#if installPrompt}
    <div class="install-banner">
      <span>Install Meatreader as an app for background temperature alerts</span>
      <div class="install-actions">
        <button class="primary" onclick={() => { void installPrompt?.prompt(); installPrompt = null; }}>Install</button>
        <button onclick={() => installPrompt = null}>Dismiss</button>
      </div>
    </div>
  {/if}

  <main class="content">
    {#if active === 'dashboard'}
      <Dashboard />
    {:else if active === 'config'}
      <Config />
    {:else if active === 'calibration'}
      <Calibration />
    {:else if active === 'diagnostics'}
      <Diagnostics />
    {:else if active === 'profiles'}
      <Profiles />
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

  /* ── Offline banner ─────────────────────────────────────────────────── */

  .offline-banner {
    display: flex;
    align-items: center;
    padding: 0.4rem 1.25rem;
    background: color-mix(in srgb, var(--color-error) 20%, var(--color-surface));
    border-bottom: 1px solid color-mix(in srgb, var(--color-error) 50%, transparent);
    color: var(--color-error);
    font-size: 0.8rem;
  }

  /* ── Install banner ─────────────────────────────────────────────────── */

  .install-banner {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 1rem;
    flex-wrap: wrap;
    padding: 0.6rem 1.25rem;
    background: color-mix(in srgb, var(--color-accent) 15%, var(--color-surface));
    border-bottom: 1px solid color-mix(in srgb, var(--color-accent) 40%, transparent);
    font-size: 0.875rem;
  }

  .install-actions {
    display: flex;
    gap: 0.5rem;
    flex-shrink: 0;
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
