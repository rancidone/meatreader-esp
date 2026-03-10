<script lang="ts">
  import { device as deviceApi, ota, ApiError } from '../lib/api/live.ts';
  import type { DeviceResponse } from '../lib/api/live.ts';

  // ── State machine ────────────────────────────────────────────────────────────

  type Phase =
    | 'idle'
    | 'selected'
    | 'confirm'
    | 'uploading'
    | 'flashing'
    | 'rebooting'
    | 'polling'
    | 'done'
    | 'error';

  let phase        = $state<Phase>('idle');
  let selectedFile = $state<File | null>(null);
  let uploadPct    = $state(0);
  let errorMsg     = $state<string | null>(null);
  let deviceInfo   = $state<DeviceResponse | null>(null);
  let dragging     = $state(false);

  // ── Load device info on mount ────────────────────────────────────────────────

  $effect(() => {
    void fetchDevice();
  });

  async function fetchDevice(): Promise<void> {
    try {
      deviceInfo = await deviceApi();
    } catch { /* non-critical; show — if unavailable */ }
  }

  // ── File selection ───────────────────────────────────────────────────────────

  function onFileInput(e: Event): void {
    const input = e.currentTarget as HTMLInputElement;
    const file  = input.files?.[0] ?? null;
    selectFile(file);
  }

  function onDrop(e: DragEvent): void {
    e.preventDefault();
    dragging = false;
    const file = e.dataTransfer?.files?.[0] ?? null;
    selectFile(file);
  }

  function onDragOver(e: DragEvent): void {
    e.preventDefault();
    dragging = true;
  }

  function onDragLeave(): void {
    dragging = false;
  }

  function selectFile(file: File | null): void {
    if (!file) return;
    selectedFile = file;
    phase        = 'selected';
    errorMsg     = null;
  }

  function clearSelection(): void {
    selectedFile = null;
    phase        = 'idle';
    errorMsg     = null;
    uploadPct    = 0;
  }

  // ── Warning modal ────────────────────────────────────────────────────────────

  function requestUpload(): void {
    phase = 'confirm';
  }

  function cancelUpload(): void {
    phase = 'selected';
  }

  // ── Upload ───────────────────────────────────────────────────────────────────

  async function startUpload(): Promise<void> {
    if (!selectedFile) return;

    phase     = 'uploading';
    uploadPct = 0;
    errorMsg  = null;

    try {
      await uploadFirmware(selectedFile);
    } catch (e) {
      phase    = 'error';
      errorMsg = e instanceof Error ? e.message : String(e);
      return;
    }

    phase = 'flashing';

    // Give device time to flash (~5 s) then transition to rebooting
    await delay(5_000);
    phase = 'rebooting';

    // Poll until device responds
    phase = 'polling';
    await pollUntilOnline();
  }

  function uploadFirmware(file: File): Promise<void> {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();

      xhr.upload.onprogress = (e: ProgressEvent) => {
        if (e.lengthComputable) {
          uploadPct = Math.round((e.loaded / e.total) * 100);
        }
      };

      xhr.onload = () => {
        if (xhr.status >= 200 && xhr.status < 300) {
          uploadPct = 100;
          resolve();
        } else {
          let msg = xhr.statusText;
          try {
            const body = JSON.parse(xhr.responseText) as { message?: string };
            if (body.message) msg = body.message;
          } catch { /* ignore */ }
          reject(new Error(`Upload failed (${xhr.status}): ${msg}`));
        }
      };

      xhr.onerror = () => reject(new Error('Network error during upload'));
      xhr.ontimeout = () => reject(new Error('Upload timed out'));

      xhr.open('POST', '/ota/upload');
      xhr.timeout = 120_000; // 2 min
      const form = new FormData();
      form.append('firmware', file, file.name);
      xhr.send(form);
    });
  }

  async function pollUntilOnline(): Promise<void> {
    const maxAttempts = 60; // 2 min max
    for (let i = 0; i < maxAttempts; i++) {
      await delay(2_000);
      try {
        deviceInfo = await deviceApi();
        phase = 'done';
        return;
      } catch { /* still rebooting */ }
    }
    phase    = 'error';
    errorMsg = 'Device did not come back online within 2 minutes.';
  }

  function delay(ms: number): Promise<void> {
    return new Promise(resolve => setTimeout(resolve, ms));
  }

  // ── Rollback ─────────────────────────────────────────────────────────────────

  let rolling = $state(false);
  let rollbackError = $state<string | null>(null);

  async function doRollback(): Promise<void> {
    rolling       = true;
    rollbackError = null;
    try {
      await ota.rollback();
      // Device will reboot; show polling state
      phase = 'polling';
      await pollUntilOnline();
    } catch (e) {
      rollbackError = e instanceof ApiError
        ? `Rollback failed (${e.httpStatus}): ${e.message}`
        : e instanceof Error ? e.message : String(e);
    } finally {
      rolling = false;
    }
  }

  // ── Derived ──────────────────────────────────────────────────────────────────

  const statusLabel = $derived((): string => {
    switch (phase) {
      case 'idle':      return 'Select a firmware .bin file to begin.';
      case 'selected':  return `Ready to flash: ${selectedFile?.name ?? ''}`;
      case 'confirm':   return 'Confirm firmware update';
      case 'uploading': return `Uploading… ${uploadPct}%`;
      case 'flashing':  return 'Flashing… Do not power off the device.';
      case 'rebooting': return 'Rebooting…';
      case 'polling':   return 'Waiting for device to come back online…';
      case 'done':      return 'Update successful!';
      case 'error':     return `Error: ${errorMsg}`;
    }
  });

  const isWorking = $derived(
    phase === 'uploading' || phase === 'flashing' || phase === 'rebooting' || phase === 'polling'
  );
</script>

<div class="col">

  <!-- ── Header ─────────────────────────────────────────────────────────── -->
  <div class="fw-header">
    <h2>Firmware Update</h2>
  </div>

  <!-- ── Current version ────────────────────────────────────────────────── -->
  <div class="card">
    <h3>Current Firmware</h3>
    <table class="info-table">
      <tbody>
        <tr>
          <th>Version</th>
          <td>{deviceInfo?.firmware_version ?? deviceInfo?.firmware ?? '—'}</td>
        </tr>
        <tr>
          <th>Platform</th>
          <td>{deviceInfo?.platform ?? '—'}</td>
        </tr>
      </tbody>
    </table>
  </div>

  <!-- ── Upload card ────────────────────────────────────────────────────── -->
  <div class="card">
    <h3>Upload New Firmware</h3>

    <!-- Drop zone (only shown when idle or selected) -->
    {#if !isWorking && phase !== 'done'}
      <!-- svelte-ignore a11y_no_static_element_interactions -->
      <div
        class="drop-zone"
        class:dragging
        ondrop={onDrop}
        ondragover={onDragOver}
        ondragleave={onDragLeave}
      >
        {#if selectedFile}
          <span class="file-name">{selectedFile.name}</span>
          <span class="file-size">({(selectedFile.size / 1024).toFixed(1)} KB)</span>
          <button class="btn-link" onclick={clearSelection}>Clear</button>
        {:else}
          <p class="drop-hint">Drag &amp; drop a <code>.bin</code> file here, or</p>
          <label class="btn-secondary">
            Browse…
            <input
              type="file"
              accept=".bin"
              style="display:none"
              oninput={onFileInput}
            />
          </label>
        {/if}
      </div>
    {/if}

    <!-- Status line -->
    <p
      class="status-line"
      class:status-error={phase === 'error'}
      class:status-done={phase === 'done'}
    >
      {statusLabel}
    </p>

    <!-- Progress bar (uploading only) -->
    {#if phase === 'uploading'}
      <div class="progress-track">
        <div class="progress-fill" style="width: {uploadPct}%"></div>
      </div>
    {/if}

    <!-- Action buttons -->
    {#if phase === 'selected'}
      <div class="btn-row">
        <button class="btn-primary" onclick={requestUpload}>Flash Firmware</button>
        <button class="btn-secondary" onclick={clearSelection}>Cancel</button>
      </div>
    {:else if phase === 'done' || phase === 'error'}
      <div class="btn-row">
        <button class="btn-secondary" onclick={clearSelection}>Start Over</button>
      </div>
    {:else if isWorking}
      <div class="btn-row">
        <span class="muted">Please wait…</span>
      </div>
    {/if}
  </div>

  <!-- ── Rollback card ───────────────────────────────────────────────────── -->
  {#if !isWorking}
    <div class="card">
      <h3>Rollback</h3>
      <p class="muted">
        Revert to the previous firmware partition. Only available if the device
        has a valid fallback slot.
      </p>
      {#if rollbackError}
        <p class="status-error">{rollbackError}</p>
      {/if}
      <div class="btn-row">
        <button
          class="btn-danger"
          onclick={doRollback}
          disabled={rolling}
        >
          {rolling ? 'Rolling back…' : 'Rollback to Previous'}
        </button>
      </div>
    </div>
  {/if}

</div>

<!-- ── Warning modal ──────────────────────────────────────────────────────── -->
{#if phase === 'confirm'}
  <!-- svelte-ignore a11y_click_events_have_key_events a11y_no_static_element_interactions -->
  <div class="modal-backdrop" onclick={cancelUpload}>
    <!-- svelte-ignore a11y_click_events_have_key_events a11y_no_static_element_interactions -->
    <div class="modal" onclick={(e) => e.stopPropagation()}>
      <h3>Confirm Firmware Update</h3>
      <p>
        This will restart the device. Active cook data will be preserved in NVS.
      </p>
      <p class="muted">File: <strong>{selectedFile?.name}</strong></p>
      <div class="modal-actions">
        <button class="btn-secondary" onclick={cancelUpload}>Cancel</button>
        <button class="btn-primary" onclick={startUpload}>Confirm &amp; Flash</button>
      </div>
    </div>
  </div>
{/if}

<style>
  .fw-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .fw-header h2 { margin: 0; }

  /* ── Info table ─────────────────────────────────────────────────────── */

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

  /* ── Drop zone ──────────────────────────────────────────────────────── */

  .drop-zone {
    border: 2px dashed var(--color-border);
    border-radius: var(--radius);
    padding: 2rem 1.5rem;
    text-align: center;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 0.5rem;
    transition: border-color 0.15s, background 0.15s;
    margin-bottom: 0.75rem;
  }

  .drop-zone.dragging {
    border-color: var(--color-accent);
    background: color-mix(in srgb, var(--color-accent) 6%, transparent);
  }

  .drop-hint {
    color: var(--color-text-muted);
    margin: 0;
    font-size: 0.9rem;
  }

  .file-name {
    font-family: var(--font-mono);
    font-size: 0.9rem;
  }

  .file-size {
    font-size: 0.8rem;
    color: var(--color-text-muted);
  }

  /* ── Status line ────────────────────────────────────────────────────── */

  .status-line {
    margin: 0.5rem 0;
    font-size: 0.9rem;
    color: var(--color-text-muted);
  }

  .status-error {
    color: var(--color-error);
  }

  .status-done {
    color: var(--color-success, #4caf50);
  }

  /* ── Progress bar ───────────────────────────────────────────────────── */

  .progress-track {
    width: 100%;
    height: 6px;
    background: var(--color-border);
    border-radius: 3px;
    overflow: hidden;
    margin: 0.5rem 0;
  }

  .progress-fill {
    height: 100%;
    background: var(--color-accent);
    border-radius: 3px;
    transition: width 0.2s ease;
  }

  /* ── Buttons ────────────────────────────────────────────────────────── */

  .btn-row {
    display: flex;
    gap: 0.5rem;
    align-items: center;
    margin-top: 0.75rem;
    flex-wrap: wrap;
  }

  .btn-primary,
  .btn-secondary,
  .btn-danger,
  .btn-link {
    border: none;
    border-radius: var(--radius);
    font-size: 0.875rem;
    font-weight: 500;
    padding: 0.4rem 0.9rem;
    cursor: pointer;
    line-height: 1.4;
  }

  .btn-primary {
    background: var(--color-accent);
    color: #fff;
  }

  .btn-primary:hover:not(:disabled) {
    filter: brightness(1.1);
  }

  .btn-secondary {
    background: var(--color-surface-alt);
    color: var(--color-text);
    display: inline-flex;
    align-items: center;
    gap: 0.25rem;
  }

  .btn-secondary:hover:not(:disabled) {
    filter: brightness(0.92);
  }

  .btn-danger {
    background: color-mix(in srgb, var(--color-error) 90%, transparent);
    color: #fff;
  }

  .btn-danger:hover:not(:disabled) {
    filter: brightness(1.1);
  }

  .btn-link {
    background: none;
    color: var(--color-accent);
    padding: 0;
    font-size: 0.85rem;
  }

  button:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  /* ── Modal ──────────────────────────────────────────────────────────── */

  .modal-backdrop {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.5);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 100;
  }

  .modal {
    background: var(--color-surface);
    border: 1px solid var(--color-border);
    border-radius: var(--radius);
    padding: 1.5rem;
    max-width: 420px;
    width: 90%;
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
  }

  .modal h3 { margin: 0; }
  .modal p  { margin: 0; font-size: 0.9rem; }

  .modal-actions {
    display: flex;
    gap: 0.5rem;
    justify-content: flex-end;
    margin-top: 0.5rem;
  }

  /* ── Misc ───────────────────────────────────────────────────────────── */

  .muted {
    color: var(--color-text-muted);
    font-size: 0.9rem;
  }
</style>
