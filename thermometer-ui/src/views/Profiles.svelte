<script lang="ts">
  import { profileStore, STARTER_LIBRARY } from '../lib/stores/profileStore.svelte.ts';
  import type { CookProfile, Stage } from '../lib/api/types.ts';
  import { configStore } from '../lib/stores/config.svelte.ts';

  // Temperature conversion helpers (UI shows °F, API uses °C)
  function cToF(c: number): number { return c * 9 / 5 + 32; }
  function fToC(f: number): number { return (f - 32) * 5 / 9; }
  function fmtF(c: number): string { return `${cToF(c).toFixed(0)}°F`; }

  // Load profiles and config on mount
  $effect(() => {
    void profileStore.fetchProfiles();
    void configStore.fetch();
  });

  // ── Channel list from config ────────────────────────────────────────────

  const channels = $derived(
    configStore.status?.active.channels ?? []
  );

  // ── Edit state ─────────────────────────────────────────────────────────

  type EditMode = 'none' | 'create' | { apiSlot: number };

  let editMode = $state<EditMode>('none');

  // Form fields
  let formName       = $state('');
  let formStages     = $state<{ label: string; target_f: string; alert_method: string }[]>([
    { label: '', target_f: '', alert_method: 'none' },
  ]);

  function resetForm(): void {
    formName   = '';
    formStages = [{ label: '', target_f: '', alert_method: 'none' }];
  }

  function openCreate(): void {
    resetForm();
    editMode = 'create';
  }

  function openEdit(slotIdx: number): void {
    const profile = profileStore.profiles[slotIdx];
    if (!profile || !profile.name) return;
    formName   = profile.name;
    formStages = profile.stages.map(s => ({
      label:        s.label,
      target_f:     cToF(s.target_temp_c).toFixed(0),
      alert_method: s.alert_method,
    }));
    editMode = { apiSlot: slotIdx };
  }

  function cancelEdit(): void {
    editMode = 'none';
    resetForm();
  }

  function addStage(): void {
    if (formStages.length >= 4) return;
    formStages = [...formStages, { label: '', target_f: '', alert_method: 'none' }];
  }

  function removeStage(i: number): void {
    formStages = formStages.filter((_, idx) => idx !== i);
  }

  function buildProfileFromForm(): CookProfile {
    const stages: Stage[] = formStages
      .filter(s => s.target_f !== '')
      .map(s => ({
        label:         s.label || `Stage ${formStages.indexOf(s) + 1}`,
        target_temp_c: fToC(Number(s.target_f)),
        alert_method:  s.alert_method,
      }));
    return {
      name:       formName.trim(),
      num_stages: stages.length,
      stages,
    };
  }

  async function saveForm(): Promise<void> {
    const profile = buildProfileFromForm();
    if (!profile.name) { alert('Profile name is required.'); return; }
    if (profile.num_stages === 0) { alert('At least one stage with a target temperature is required.'); return; }

    if (editMode === 'create') {
      // Find first empty slot
      const emptyIdx = profileStore.profiles.findIndex(p => !p.name);
      if (emptyIdx === -1) {
        alert('All 8 profile slots are in use. Delete one first.');
        return;
      }
      await profileStore.saveProfile(emptyIdx, profile);
    } else if (typeof editMode === 'object' && 'apiSlot' in editMode) {
      await profileStore.saveProfile(editMode.apiSlot, profile);
    }

    if (!profileStore.error) {
      editMode = 'none';
      resetForm();
    }
  }

  async function deleteSlot(slotIdx: number): Promise<void> {
    const profile = profileStore.profiles[slotIdx];
    if (!profile?.name) return;
    if (!confirm(`Delete profile "${profile.name}"?`)) return;
    await profileStore.deleteProfile(slotIdx);
  }

  // ── Apply profile to channel ────────────────────────────────────────────

  async function applyApiProfile(slotIdx: number, channelIdx: number): Promise<void> {
    await profileStore.applyProfileToChannel(channelIdx, `api:${slotIdx}`);
  }

  async function applyStarterProfile(starterIdx: number, channelIdx: number): Promise<void> {
    await profileStore.applyProfileToChannel(channelIdx, `starter:${starterIdx}`);
  }

  // Per-profile "apply to channel" dropdown state
  let applyTargetByApiSlot     = $state<Record<number, number>>({});
  let applyTargetByStarterSlot = $state<Record<number, number>>({});
</script>

<div class="col">

  <!-- ── Header ─────────────────────────────────────────────────────────── -->
  <div class="page-header">
    <h1>Cook Profiles</h1>
    {#if profileStore.error}
      <span class="badge error">{profileStore.error}</span>
    {/if}
  </div>

  <p class="muted hint-text">
    Profiles set temperature targets for each cook stage. Select a profile on a channel card to
    activate it. Starter library profiles are read-only; create custom profiles in the slots below.
  </p>

  <!-- ── Starter Library ────────────────────────────────────────────────── -->
  <section class="card col">
    <h2>Starter Library <span class="muted">(read-only)</span></h2>

    {#each STARTER_LIBRARY as preset, i}
      <div class="profile-row">
        <div class="profile-info">
          <span class="profile-name">{preset.name}</span>
          <span class="profile-stages muted">
            {#each preset.stages as stage, si}
              {si > 0 ? ' → ' : ''}{stage.label}: {fmtF(stage.target_temp_c)}
            {/each}
          </span>
        </div>

        {#if channels.length > 0}
          <div class="apply-row">
            <select
              bind:value={applyTargetByStarterSlot[i]}
              aria-label="Select channel"
            >
              <option value={undefined}>Channel…</option>
              {#each channels as ch, ci}
                <option value={ci}>{ch.label || `Channel ${ch.adc_channel}`}</option>
              {/each}
            </select>
            <button
              class="primary"
              onclick={() => {
                const ch = applyTargetByStarterSlot[i];
                if (ch !== undefined) void applyStarterProfile(i, ch);
              }}
              disabled={applyTargetByStarterSlot[i] === undefined || profileStore.loading}
            >Apply</button>
          </div>
        {/if}
      </div>
    {/each}
  </section>

  <!-- ── Custom Profile Slots ───────────────────────────────────────────── -->
  <section class="card col">
    <div class="section-header">
      <h2>Custom Profiles</h2>
      {#if editMode === 'none'}
        <button onclick={openCreate} disabled={profileStore.loading}>+ New profile</button>
      {/if}
    </div>

    {#if profileStore.loading && profileStore.profiles.length === 0}
      <p class="muted">Loading…</p>
    {:else}
      {#each profileStore.profiles as profile, i}
        {#if profile.name}
          <!-- Filled slot -->
          <div class="profile-row">
            {#if typeof editMode === 'object' && editMode.apiSlot === i}
              <!-- Inline edit form -->
              <div class="edit-form col">
                <div class="field">
                  <label for="edit-name-{i}">Profile name</label>
                  <input id="edit-name-{i}" type="text" bind:value={formName} maxlength="31" />
                </div>

                {#each formStages as stage, si}
                  <div class="stage-row">
                    <div class="field">
                      <label for="edit-label-{i}-{si}">Stage {si + 1} label</label>
                      <input
                        id="edit-label-{i}-{si}"
                        type="text"
                        bind:value={formStages[si].label}
                        placeholder="e.g. Wrap"
                      />
                    </div>
                    <div class="field">
                      <label for="edit-temp-{i}-{si}">Target (°F)</label>
                      <input
                        id="edit-temp-{i}-{si}"
                        type="number"
                        step="1"
                        min="32"
                        max="500"
                        bind:value={formStages[si].target_f}
                      />
                    </div>
                    <div class="field">
                      <label for="edit-method-{i}-{si}">Alert</label>
                      <select id="edit-method-{i}-{si}" bind:value={formStages[si].alert_method}>
                        <option value="none">None</option>
                        <option value="webhook">Webhook</option>
                        <option value="mqtt">MQTT</option>
                      </select>
                    </div>
                    {#if formStages.length > 1}
                      <button class="icon-btn danger" onclick={() => removeStage(si)} title="Remove stage">✕</button>
                    {/if}
                  </div>
                {/each}

                {#if formStages.length < 4}
                  <button class="small" onclick={addStage}>+ Add stage</button>
                {/if}

                <div class="edit-actions">
                  <button class="primary" onclick={() => void saveForm()} disabled={profileStore.loading}>Save</button>
                  <button onclick={cancelEdit}>Cancel</button>
                </div>
              </div>
            {:else}
              <div class="profile-info">
                <span class="profile-name">{profile.name}</span>
                <span class="profile-stages muted">
                  {#each profile.stages as stage, si}
                    {si > 0 ? ' → ' : ''}{stage.label}: {fmtF(stage.target_temp_c)}
                  {/each}
                </span>
              </div>

              <div class="slot-actions">
                {#if channels.length > 0}
                  <div class="apply-row">
                    <select
                      bind:value={applyTargetByApiSlot[i]}
                      aria-label="Select channel"
                    >
                      <option value={undefined}>Channel…</option>
                      {#each channels as ch, ci}
                        <option value={ci}>{ch.label || `Channel ${ch.adc_channel}`}</option>
                      {/each}
                    </select>
                    <button
                      class="primary"
                      onclick={() => {
                        const ch = applyTargetByApiSlot[i];
                        if (ch !== undefined) void applyApiProfile(i, ch);
                      }}
                      disabled={applyTargetByApiSlot[i] === undefined || profileStore.loading}
                    >Apply</button>
                  </div>
                {/if}
                {#if editMode === 'none'}
                  <button onclick={() => openEdit(i)} disabled={profileStore.loading}>Edit</button>
                  <button class="danger" onclick={() => void deleteSlot(i)} disabled={profileStore.loading}>Delete</button>
                {/if}
              </div>
            {/if}
          </div>
        {:else}
          <!-- Empty slot — shown only when in create mode targeting this slot -->
          <div class="profile-row empty">
            <span class="muted">Slot {i + 1} — empty</span>
          </div>
        {/if}
      {/each}

      <!-- Create form (shown after the list) -->
      {#if editMode === 'create'}
        <div class="edit-form col">
          <h3>New profile</h3>

          <div class="field">
            <label for="create-name">Profile name</label>
            <input id="create-name" type="text" bind:value={formName} maxlength="31" placeholder="e.g. My Brisket" />
          </div>

          {#each formStages as stage, si}
            <div class="stage-row">
              <div class="field">
                <label for="create-label-{si}">Stage {si + 1} label</label>
                <input
                  id="create-label-{si}"
                  type="text"
                  bind:value={formStages[si].label}
                  placeholder="e.g. Wrap"
                />
              </div>
              <div class="field">
                <label for="create-temp-{si}">Target (°F)</label>
                <input
                  id="create-temp-{si}"
                  type="number"
                  step="1"
                  min="32"
                  max="500"
                  bind:value={formStages[si].target_f}
                />
              </div>
              <div class="field">
                <label for="create-method-{si}">Alert</label>
                <select id="create-method-{si}" bind:value={formStages[si].alert_method}>
                  <option value="none">None</option>
                  <option value="webhook">Webhook</option>
                  <option value="mqtt">MQTT</option>
                </select>
              </div>
              {#if formStages.length > 1}
                <button class="icon-btn danger" onclick={() => removeStage(si)} title="Remove stage">✕</button>
              {/if}
            </div>
          {/each}

          {#if formStages.length < 4}
            <button class="small" onclick={addStage}>+ Add stage</button>
          {/if}

          <div class="edit-actions">
            <button class="primary" onclick={() => void saveForm()} disabled={profileStore.loading}>Save</button>
            <button onclick={cancelEdit}>Cancel</button>
          </div>
        </div>
      {/if}
    {/if}
  </section>

</div>

<style>
  .page-header {
    display: flex;
    align-items: center;
    gap: var(--gap-sm);
  }

  .hint-text {
    margin: 0;
    font-size: 0.85rem;
  }

  .section-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .profile-row {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    gap: var(--gap-sm);
    padding: var(--gap-sm) 0;
    border-bottom: 1px solid var(--color-border);
    flex-wrap: wrap;
  }

  .profile-row:last-child {
    border-bottom: none;
  }

  .profile-row.empty {
    opacity: 0.5;
    font-size: 0.82rem;
  }

  .profile-info {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    flex: 1;
    min-width: 0;
  }

  .profile-name {
    font-weight: 600;
    font-size: 0.95rem;
  }

  .profile-stages {
    font-size: 0.8rem;
    font-family: var(--font-mono);
  }

  .slot-actions {
    display: flex;
    align-items: center;
    gap: var(--gap-xs);
    flex-wrap: wrap;
  }

  .apply-row {
    display: flex;
    align-items: center;
    gap: var(--gap-xs);
  }

  .apply-row select {
    background: var(--color-surface-alt);
    border: 1px solid var(--color-border);
    border-radius: var(--radius);
    color: var(--color-text);
    padding: 0.35rem 0.5rem;
    font-size: 0.85rem;
  }

  /* ── Edit form ────────────────────────────────────────────────────────── */

  .edit-form {
    width: 100%;
    padding: var(--gap-sm) 0;
  }

  .stage-row {
    display: grid;
    grid-template-columns: 1fr 120px 130px auto;
    gap: var(--gap-xs);
    align-items: end;
  }

  .field {
    display: flex;
    flex-direction: column;
    gap: var(--gap-xs);
  }

  label {
    font-size: 0.75rem;
    color: var(--color-text-muted);
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  .edit-actions {
    display: flex;
    gap: var(--gap-xs);
    margin-top: var(--gap-xs);
  }

  button.small {
    font-size: 0.78rem;
    padding: 0.2rem 0.6rem;
    align-self: flex-start;
  }

  button.danger {
    background: color-mix(in srgb, var(--color-error) 15%, var(--color-surface-alt));
    border-color: var(--color-error);
    color: var(--color-error);
  }

  button.danger:hover:not(:disabled) {
    background: color-mix(in srgb, var(--color-error) 25%, var(--color-surface-alt));
  }

  .icon-btn {
    padding: 0.3rem 0.5rem;
    font-size: 0.85rem;
    align-self: center;
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
