/**
 * Bridge: forwards SSE snapshots to the service worker so it can fire
 * Notification API alerts even when the tab is backgrounded.
 */

export async function postSnapshotToSW(snapshot: unknown): Promise<void> {
  if (!('serviceWorker' in navigator)) return;
  try {
    const reg = await navigator.serviceWorker.ready;
    reg.active?.postMessage({ type: 'snapshot', snapshot });
  } catch {
    // Service worker not available — silently skip
  }
}

export async function clearSWNotifications(): Promise<void> {
  if (!('serviceWorker' in navigator)) return;
  try {
    const reg = await navigator.serviceWorker.ready;
    reg.active?.postMessage({ type: 'clear-notifications' });
  } catch {
    // ignore
  }
}
