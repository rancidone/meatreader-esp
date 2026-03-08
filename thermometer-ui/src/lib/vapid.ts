/**
 * VAPID (Voluntary Application Server Identification) client utilities.
 *
 * Generates a P-256 ECDH key pair and stores it in localStorage. The public
 * key is used as the `applicationServerKey` when calling
 * `pushManager.subscribe`. The private key would be needed server-side to
 * sign Web Push requests — on a local ESP32 this requires mbedtls ECDSA P-256
 * (supported in ESP-IDF). See firmware TODO: POST /push/notify.
 *
 * For now this module handles the client side: key generation, persistence,
 * and push subscription management. The firmware endpoint is deferred.
 */

const STORAGE_KEY = 'meatreader-vapid-keys';

export interface VapidKeys {
  /** Base64url-encoded uncompressed P-256 public key (65 bytes) */
  publicKey: string;
  /** Base64url-encoded private key (32 bytes) — keep local, never send to device */
  privateKey: string;
}

/** Load persisted VAPID keys from localStorage, or null if not generated yet. */
export function loadVapidKeys(): VapidKeys | null {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    return raw ? (JSON.parse(raw) as VapidKeys) : null;
  } catch {
    return null;
  }
}

/** Generate a new P-256 ECDH key pair and persist it in localStorage. */
export async function generateVapidKeys(): Promise<VapidKeys> {
  const keyPair = await crypto.subtle.generateKey(
    { name: 'ECDH', namedCurve: 'P-256' },
    true,
    ['deriveKey']
  );

  const [pubRaw, privJwk] = await Promise.all([
    crypto.subtle.exportKey('raw', keyPair.publicKey),
    crypto.subtle.exportKey('jwk', keyPair.privateKey),
  ]);

  const keys: VapidKeys = {
    publicKey: arrayBufferToBase64url(pubRaw),
    privateKey: privJwk.d ?? '',
  };
  localStorage.setItem(STORAGE_KEY, JSON.stringify(keys));
  return keys;
}

/** Get existing keys or generate new ones. */
export async function getOrCreateVapidKeys(): Promise<VapidKeys> {
  return loadVapidKeys() ?? (await generateVapidKeys());
}

/** Subscribe to Web Push using the VAPID public key.
 *  Returns the PushSubscription, or null if push is not supported/denied. */
export async function subscribeToPush(): Promise<PushSubscription | null> {
  if (!('PushManager' in window)) return null;
  if (Notification.permission === 'denied') return null;
  if (Notification.permission === 'default') {
    const perm = await Notification.requestPermission();
    if (perm !== 'granted') return null;
  }

  const keys = await getOrCreateVapidKeys();
  const reg = await navigator.serviceWorker.ready;

  try {
    const existing = await reg.pushManager.getSubscription();
    if (existing) return existing;

    return await reg.pushManager.subscribe({
      userVisibleOnly: true,
      applicationServerKey: base64urlToUint8Array(keys.publicKey),
    });
  } catch {
    return null;
  }
}

/** Unsubscribe from Web Push. */
export async function unsubscribeFromPush(): Promise<void> {
  if (!('PushManager' in window)) return;
  const reg = await navigator.serviceWorker.ready;
  const sub = await reg.pushManager.getSubscription();
  if (sub) await sub.unsubscribe();
}

// ── Helpers ──────────────────────────────────────────────────────────────────

function arrayBufferToBase64url(buf: ArrayBuffer): string {
  const bytes = new Uint8Array(buf);
  let str = '';
  for (const b of bytes) str += String.fromCharCode(b);
  return btoa(str).replace(/\+/g, '-').replace(/\//g, '_').replace(/=/g, '');
}

function base64urlToUint8Array(b64url: string): Uint8Array {
  const b64 = b64url.replace(/-/g, '+').replace(/_/g, '/');
  const raw = atob(b64);
  const arr = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) arr[i] = raw.charCodeAt(i);
  return arr;
}
