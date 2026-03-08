<script lang="ts">
  import type { ChannelReading, Snapshot } from '../../api/types.ts';
  import type { TempUnit } from '../../stores/ui.svelte.ts';
  import type { ChannelPrediction } from '../../stores/predictions.svelte.ts';
  import { formatTemp } from '../../utils/format.ts';
  import QualityBadge from '../common/QualityBadge.svelte';
  import Sparkline    from './Sparkline.svelte';
  import { cookStore } from '../../stores/cookStore.svelte.ts';
  import { profileStore, STARTER_LIBRARY } from '../../stores/profileStore.svelte.ts';
  import type { ProfileRef } from '../../stores/profileStore.svelte.ts';

  interface Props {
    reading:     ChannelReading | null;
    snapshots:   Snapshot[];
    unit:        TempUnit;
    color:       string;
    label?:      string;
    channelIdx:  number;           // index into config.channels / alerts arrays
    prediction?: ChannelPrediction;
    onclick?:    () => void;
  }
  const { reading, snapshots, unit, color, label, channelIdx, prediction, onclick }: Props = $props();

  // ── Cook timer ──────────────────────────────────────────────────────────────

  let nowMs = $state(Date.now());

  $effect(() => {
    const id = setInterval(() => { nowMs = Date.now(); }, 1000);
    return () => clearInterval(id);
  });

  const channelId = $derived(reading?.id ?? -1);

  const cookStart = $derived(
    channelId >= 0 ? (cookStore.cookStartTime[channelId] ?? null) : null
  );

  const elapsedMs = $derived(
    cookStart !== null ? Math.max(0, nowMs - cookStart) : null
  );

  function formatElapsed(ms: number): string {
    const totalSec = Math.floor(ms / 1000);
    const h   = Math.floor(totalSec / 3600);
    const m   = Math.floor((totalSec % 3600) / 60);
    const s   = totalSec % 60;
    return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
  }

  // ── Predicted done time ──────────────────────────────────────────────────────

  function formatEta(ms: number): string {
    const totalMin = Math.floor(ms / 60000);
    const h = Math.floor(totalMin / 60);
    const m = totalMin % 60;
    if (h > 0) return `${h}h ${m}m`;
    return `${m}m`;
  }

  const etaMs = $derived(prediction?.eta_ms ?? null);

  // ── Stall detection ─────────────────────────────────────────────────────────

  const stallDetected = $derived(reading?.stall_detected === true);

  const stallElapsedMs = $derived(() => {
    if (!stallDetected) return null;
    const stallAt = reading?.stall_at;
    if (stallAt == null) return null;
    return Math.max(0, nowMs - stallAt);
  });

  // ── Profile selector ────────────────────────────────────────────────────────

  // Build a flat list of all profile options: starter library + custom API profiles
  const allProfileOptions = $derived(() => {
    const opts: { ref: ProfileRef; name: string }[] = [];
    STARTER_LIBRARY.forEach((p, i) => {
      opts.push({ ref: `starter:${i}`, name: p.name });
    });
    profileStore.profiles.forEach((p, i) => {
      if (p.name) opts.push({ ref: `api:${i}`, name: p.name });
    });
    return opts;
  });

  const selectedRef = $derived(
    profileStore.selectedProfileByChannel.get(channelIdx) ?? null
  );

  const activeProfile = $derived(
    selectedRef !== null ? profileStore.resolveProfile(selectedRef) : null
  );

  // Controlled select value — empty string means "no profile"
  let selectValue = $state('');

  $effect(() => {
    selectValue = selectedRef ?? '';
  });

  function onProfileChange(e: Event): void {
    const val = (e.target as HTMLSelectElement).value;
    if (!val) {
      profileStore.clearChannelProfile(channelIdx);
    } else {
      void profileStore.applyProfileToChannel(channelIdx, val as ProfileRef);
    }
  }

  // ── Multi-stage cook progress ────────────────────────────────────────────────

  const currentStageIdx = $derived(() => {
    if (!activeProfile || !reading || reading.quality !== 'ok') return null;
    const tempC = reading.temperature_c;
    // Find first stage not yet reached
    for (let i = 0; i < activeProfile.stages.length; i++) {
      if (tempC < activeProfile.stages[i].target_temp_c) return i;
    }
    // All stages done
    return activeProfile.stages.length;
  });

  const currentStage = $derived(() => {
    const idx = currentStageIdx();
    if (idx === null || !activeProfile) return null;
    if (idx >= activeProfile.stages.length) return null;
    return activeProfile.stages[idx];
  });

  const nextStage = $derived(() => {
    const idx = currentStageIdx();
    if (idx === null || !activeProfile) return null;
    const nextIdx = (idx ?? 0) + 1;
    return activeProfile.stages[nextIdx] ?? null;
  });

  function cToF(c: number): string {
    return `${(c * 9 / 5 + 32).toFixed(0)}°F`;
  }

  const allStagesDone = $derived(() => {
    const idx = currentStageIdx();
    if (idx === null || !activeProfile) return false;
    return idx >= activeProfile.stages.length;
  });
</script>

<div class="card channel-card">
  <div class="card-header">
    <h2>{label || `Channel ${reading?.id ?? '—'}`}</h2>
    <div class="badge-group">
      {#if stallDetected}
        <span class="stall-badge">
          Stall
          {#if stallElapsedMs() !== null}
            · {formatElapsed(stallElapsedMs()!)}
          {/if}
        </span>
      {/if}
      {#if reading?.alert_triggered === true}
        <span class="alert-badge">Alert!</span>
      {/if}
      {#if reading}
        <QualityBadge quality={reading.quality} />
      {:else}
        <span class="badge disabled">—</span>
      {/if}
    </div>
  </div>

  <div class="temp" style="color: {reading?.quality === 'ok' ? color : 'var(--color-text-muted)'}">
    {reading?.quality === 'ok' ? formatTemp(reading.temperature_c, unit) : '— °' + unit}
  </div>

  <!-- Profile selector -->
  <div class="profile-selector">
    <select
      value={selectValue}
      onchange={onProfileChange}
      aria-label="Cook profile"
      disabled={profileStore.loading}
    >
      <option value="">No profile</option>
      {#if STARTER_LIBRARY.length > 0}
        <optgroup label="Starter Library">
          {#each STARTER_LIBRARY as p, i}
            <option value={`starter:${i}`}>{p.name}</option>
          {/each}
        </optgroup>
      {/if}
      {#if profileStore.profiles.some(p => p.name)}
        <optgroup label="Custom">
          {#each profileStore.profiles as p, i}
            {#if p.name}
              <option value={`api:${i}`}>{p.name}</option>
            {/if}
          {/each}
        </optgroup>
      {/if}
    </select>
  </div>

  <!-- Multi-stage cook progress -->
  {#if activeProfile && activeProfile.num_stages > 1 && reading?.quality === 'ok'}
    <div class="stage-progress">
      {#if allStagesDone()}
        <span class="stage-done">All stages complete</span>
      {:else if currentStage() !== null}
        {@const stageIdx = currentStageIdx() ?? 0}
        <span class="stage-current">
          Stage {stageIdx + 1}/{activeProfile.num_stages}: {currentStage()!.label}
          → {unit === 'F' ? cToF(currentStage()!.target_temp_c) : `${currentStage()!.target_temp_c.toFixed(1)}°C`}
        </span>
        {#if nextStage() !== null}
          <span class="stage-next muted">
            Next: {nextStage()!.label}
            → {unit === 'F' ? cToF(nextStage()!.target_temp_c) : `${nextStage()!.target_temp_c.toFixed(1)}°C`}
          </span>
        {/if}
      {/if}
    </div>
  {:else if activeProfile && activeProfile.num_stages === 1 && reading?.quality === 'ok'}
    <div class="stage-progress">
      {#if allStagesDone()}
        <span class="stage-done">Target reached: {activeProfile.stages[0].label}</span>
      {:else}
        <span class="stage-current">
          Target: {activeProfile.stages[0].label}
          → {unit === 'F' ? cToF(activeProfile.stages[0].target_temp_c) : `${activeProfile.stages[0].target_temp_c.toFixed(1)}°C`}
        </span>
      {/if}
    </div>
  {/if}

  <!-- Cook timer display & controls -->
  {#if reading?.quality === 'ok'}
    <div class="cook-row">
      {#if elapsedMs !== null}
        <span class="cook-elapsed" style="color: {color}">
          {formatElapsed(elapsedMs)}
        </span>
        <button
          class="cook-btn stop"
          onclick={() => cookStore.stopCook(channelId)}
        >Stop cook</button>
      {:else}
        <button
          class="cook-btn start"
          onclick={() => cookStore.startCook(channelId)}
        >Start cook</button>
      {/if}
    </div>
  {/if}

  <!-- Predicted done time -->
  {#if etaMs !== null}
    <div class="eta-row">
      <span class="eta-label">Done in ~{formatEta(etaMs)}</span>
    </div>
  {/if}

  <!-- svelte-ignore a11y_click_events_have_key_events a11y_no_static_element_interactions -->
  <div class="sparkline-wrap" class:clickable={!!onclick} {onclick}>
    <Sparkline {snapshots} channelId={reading?.id ?? 0} {unit} {color} />
    {#if onclick}
      <span class="expand-hint">expand ↗</span>
    {/if}
  </div>
</div>

<style>
  .channel-card {
    display: flex;
    flex-direction: column;
    gap: var(--gap-sm);
  }

  .card-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    flex-wrap: wrap;
    gap: var(--gap-xs);
  }

  .badge-group {
    display: flex;
    align-items: center;
    gap: var(--gap-xs);
    flex-wrap: wrap;
  }

  .alert-badge {
    display: inline-flex;
    align-items: center;
    gap: 0.2em;
    padding: 0.2rem 0.5rem;
    border-radius: var(--radius);
    background: color-mix(in srgb, var(--color-error) 15%, var(--color-surface-alt));
    border: 1px solid var(--color-error);
    color: var(--color-error);
    font-size: 0.72rem;
    font-weight: 600;
    letter-spacing: 0.03em;
    text-transform: uppercase;
  }

  .stall-badge {
    display: inline-flex;
    align-items: center;
    gap: 0.2em;
    padding: 0.2rem 0.5rem;
    border-radius: var(--radius);
    background: color-mix(in srgb, var(--color-warn) 15%, var(--color-surface-alt));
    border: 1px solid var(--color-warn);
    color: var(--color-warn);
    font-size: 0.72rem;
    font-weight: 600;
    letter-spacing: 0.03em;
    text-transform: uppercase;
    font-family: var(--font-mono);
  }

  .temp {
    font-size: 2.4rem;
    font-weight: 700;
    letter-spacing: -0.02em;
    line-height: 1;
    padding: 0.25rem 0;
  }

  /* ── Profile selector ────────────────────────────────────────────────────── */

  .profile-selector select {
    background: var(--color-surface-alt);
    border: 1px solid var(--color-border);
    border-radius: var(--radius);
    color: var(--color-text);
    padding: 0.3rem 0.5rem;
    font-size: 0.8rem;
    width: 100%;
  }

  .profile-selector select:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }

  /* ── Stage progress ──────────────────────────────────────────────────────── */

  .stage-progress {
    display: flex;
    flex-direction: column;
    gap: 2px;
    font-size: 0.8rem;
    font-family: var(--font-mono);
    padding: 0.3rem 0.5rem;
    background: color-mix(in srgb, var(--color-accent) 6%, var(--color-surface-alt));
    border: 1px solid color-mix(in srgb, var(--color-accent) 20%, var(--color-border));
    border-radius: var(--radius);
  }

  .stage-current {
    color: var(--color-text);
    font-weight: 600;
  }

  .stage-next {
    font-size: 0.74rem;
  }

  .stage-done {
    color: var(--color-accent);
    font-weight: 600;
  }

  /* ── Cook timer ──────────────────────────────────────────────────────────── */

  .cook-row {
    display: flex;
    align-items: center;
    gap: var(--gap-sm);
  }

  .cook-elapsed {
    font-family: var(--font-mono);
    font-size: 1.1rem;
    font-weight: 600;
    letter-spacing: 0.04em;
    flex: 1;
  }

  .cook-btn {
    font-size: 0.75rem;
    padding: 0.2rem 0.6rem;
    font-family: var(--font-mono);
    border-radius: var(--radius);
    cursor: pointer;
  }

  .cook-btn.start {
    background: transparent;
    border: 1px solid var(--color-border);
    color: var(--color-text-muted);
  }

  .cook-btn.start:hover {
    border-color: var(--color-text);
    color: var(--color-text);
  }

  .cook-btn.stop {
    background: color-mix(in srgb, var(--color-error) 12%, transparent);
    border: 1px solid color-mix(in srgb, var(--color-error) 40%, transparent);
    color: var(--color-error);
  }

  /* ── ETA ─────────────────────────────────────────────────────────────────── */

  .eta-row {
    font-size: 0.82rem;
    color: var(--color-text-muted);
  }

  .eta-label {
    font-family: var(--font-mono);
  }

  .sparkline-wrap {
    position: relative;
  }

  .sparkline-wrap.clickable {
    cursor: pointer;
  }

  .expand-hint {
    position: absolute;
    bottom: 4px;
    right: 6px;
    font-size: 0.65rem;
    color: var(--color-text-muted);
    opacity: 0;
    transition: opacity 0.15s;
    pointer-events: none;
  }

  .sparkline-wrap.clickable:hover .expand-hint {
    opacity: 1;
  }
</style>
