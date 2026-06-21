<script lang="ts">
  import { untrack } from 'svelte';
  import { tempsStore } from '../lib/stores/temps.svelte.ts';
  import { deviceStore } from '../lib/stores/device.svelte.ts';
  import { consoleLog } from '../lib/stores/consoleLog.svelte.ts';

  let logEl = $state<HTMLElement | undefined>(undefined);
  let autoScroll = $state(true);

  // Plain tracking vars — not $state, so writes don't trigger effects.
  let prevError: string | null = null;
  let prevConnected: boolean | null = null;

  // Log SSE errors and reconnects only — not every snapshot.
  $effect(() => {
    const err = tempsStore.error;
    if (err !== prevError) {
      const wasError = prevError;
      prevError = err;
      if (err) untrack(() => consoleLog.push('error', `SSE: ${err}`));
      else if (wasError !== null) untrack(() => consoleLog.push('info', 'SSE: reconnected'));
    }
  });

  // Log device connectivity changes.
  $effect(() => {
    const connected = deviceStore.connected;
    if (prevConnected !== null && connected !== prevConnected) {
      const msg = connected
        ? 'device: connected'
        : `device: offline — ${untrack(() => deviceStore.error) ?? 'unreachable'}`;
      untrack(() => consoleLog.push(connected ? 'info' : 'warn', msg));
    }
    prevConnected = connected;
  });

  // Auto-scroll: runs when entries change; reads autoScroll without tracking it
  // so the scroll event → onScroll → autoScroll write doesn't loop.
  $effect(() => {
    void consoleLog.entries.length;
    if (untrack(() => autoScroll) && logEl) {
      logEl.scrollTop = logEl.scrollHeight;
    }
  });

  function onScroll() {
    if (!logEl) return;
    autoScroll = logEl.scrollHeight - logEl.scrollTop - logEl.clientHeight < 8;
  }

  function fmtTime(ts: number): string {
    const d = new Date(ts);
    return [d.getHours(), d.getMinutes(), d.getSeconds()]
      .map(n => String(n).padStart(2, '0'))
      .join(':') + '.' + String(d.getMilliseconds()).padStart(3, '0');
  }
</script>

<div class="col">
  <div class="console-header">
    <h2>Console</h2>
    <div class="controls">
      <label class="autoscroll-label">
        <input type="checkbox" bind:checked={autoScroll} />
        Auto-scroll
      </label>
      <button onclick={() => consoleLog.clear()}>Clear</button>
    </div>
  </div>

  <div class="log-pane" bind:this={logEl} onscroll={onScroll}>
    {#if consoleLog.entries.length === 0}
      <div class="empty">Waiting for events…</div>
    {:else}
      {#each consoleLog.entries as entry (entry.id)}
        <div class="entry {entry.level}">
          <span class="ts">{fmtTime(entry.ts)}</span>
          <span class="lvl">{entry.level.toUpperCase()}</span>
          <span class="msg">{entry.message}</span>
        </div>
      {/each}
    {/if}
  </div>
</div>

<style>
  .console-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .console-header h2 { margin: 0; }

  .controls {
    display: flex;
    align-items: center;
    gap: var(--gap-sm);
  }

  .autoscroll-label {
    display: flex;
    align-items: center;
    gap: var(--gap-xs);
    font-size: 0.85rem;
    color: var(--color-text-muted);
    cursor: pointer;
    user-select: none;
  }

  .log-pane {
    background: var(--color-surface);
    border: 1px solid var(--color-border);
    border-radius: var(--radius);
    padding: 0.5rem;
    overflow-y: auto;
    flex: 1;
    min-height: 400px;
    max-height: calc(100dvh - 14rem);
    font-family: var(--font-mono);
    font-size: 0.8rem;
    line-height: 1.5;
  }

  .empty {
    color: var(--color-text-muted);
    padding: 0.5rem;
  }

  .entry {
    display: flex;
    gap: 0.75rem;
    padding: 0.1rem 0.25rem;
    border-radius: 3px;
  }

  .entry:hover { background: var(--color-surface-alt); }

  .ts {
    color: var(--color-text-muted);
    flex-shrink: 0;
    letter-spacing: 0.02em;
  }

  .lvl {
    flex-shrink: 0;
    width: 4.5ch;
    font-weight: 600;
    letter-spacing: 0.05em;
  }

  .entry.info  .lvl { color: var(--color-text-muted); }
  .entry.warn  .lvl { color: var(--color-warn); }
  .entry.error .lvl { color: var(--color-error); }

  .entry.warn  { background: color-mix(in srgb, var(--color-warn)  6%, transparent); }
  .entry.error { background: color-mix(in srgb, var(--color-error) 8%, transparent); }

  .msg {
    color: var(--color-text);
    word-break: break-all;
  }

  .entry.warn  .msg { color: var(--color-warn); }
  .entry.error .msg { color: var(--color-error); }
</style>
