/// <reference lib="webworker" />
declare const self: ServiceWorkerGlobalScope;

// Track notification state per channel to avoid repeat-firing on the same
// triggered state. Use string keys so stall alerts share the same Set.
const notified = new Set<string>();
const channelTriggered = new Map<number, boolean>();

self.addEventListener('install', () => {
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(self.clients.claim());
});

interface ChannelSnapshot {
  ch: number;
  temp_c: number;
  temp_f?: number;
  stall_detected?: boolean;
  label?: string;
  alert?: {
    enabled: boolean;
    target_temp_c: number;
    triggered: boolean;
  };
}

interface Snapshot {
  channels: ChannelSnapshot[];
}

function handleSnapshot(snapshot: Snapshot) {
  if (!snapshot?.channels) return;

  for (const ch of snapshot.channels) {
    const wasTriggered = channelTriggered.get(ch.ch) ?? false;
    const isTriggered = ch.alert?.triggered ?? false;
    const isStall = ch.stall_detected ?? false;
    const label = ch.label || `Channel ${ch.ch}`;
    const tempStr = ch.temp_f != null
      ? `${ch.temp_f.toFixed(1)}°F`
      : `${ch.temp_c.toFixed(1)}°C`;

    // Temperature target reached
    if (isTriggered && !wasTriggered && !notified.has(`alert-${ch.ch}`)) {
      notified.add(`alert-${ch.ch}`);
      self.registration.showNotification('Meatreader — Temperature Reached', {
        body: `${label} hit target: ${tempStr}`,
        icon: '/icons/icon-192.png',
        badge: '/icons/icon-192.png',
        tag: `alert-ch-${ch.ch}`,
        requireInteraction: true,
      } as NotificationOptions);
    }

    // Reset so it can fire again if the channel re-triggers
    if (!isTriggered) {
      notified.delete(`alert-${ch.ch}`);
    }

    channelTriggered.set(ch.ch, isTriggered);

    // Stall detection
    if (isStall && !notified.has(`stall-${ch.ch}`)) {
      notified.add(`stall-${ch.ch}`);
      self.registration.showNotification('Meatreader — Stall Detected', {
        body: `${label} temperature has stalled`,
        icon: '/icons/icon-192.png',
        tag: `stall-ch-${ch.ch}`,
      } as NotificationOptions);
    } else if (!isStall) {
      notified.delete(`stall-${ch.ch}`);
    }
  }
}

self.addEventListener('message', (event) => {
  const data = event.data;
  if (!data || typeof data !== 'object') return;

  if (data.type === 'snapshot') {
    handleSnapshot(data.snapshot as Snapshot);
  } else if (data.type === 'clear-notifications') {
    notified.clear();
    channelTriggered.clear();
  }
});

self.addEventListener('notificationclick', (event) => {
  event.notification.close();
  event.waitUntil(
    self.clients.matchAll({ type: 'window' }).then((clients) => {
      const existing = clients.find((c) => c.url.startsWith(self.location.origin));
      if (existing) return existing.focus();
      return self.clients.openWindow('/');
    })
  );
});
