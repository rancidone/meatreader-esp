// Live data transport layer — SSE via EventSource('/events').
//
// The firmware pushes a JSON snapshot on each sensor tick.  Consumers call
// connectSSE({ onSnapshot, onError }) to subscribe; the returned stop()
// function tears the connection down.
//
// All other API verbs (config, calibration, status, device) are still
// fetched on demand — they don't benefit from a push stream.

import type { Snapshot } from './types.ts';

export interface SSEHandle {
  stop: () => void;
}

export interface SSECallbacks {
  onSnapshot: (snap: Snapshot) => void;
  onError: (message: string) => void;
}

/**
 * Open an EventSource connection to /events and invoke callbacks on each
 * incoming snapshot.  Returns a handle with a stop() method to close the
 * connection.
 *
 * The EventSource is re-used until stop() is called — no automatic reconnect
 * beyond the browser's built-in behaviour (which retries on network loss).
 */
export function connectSSE(callbacks: SSECallbacks): SSEHandle {
  const es = new EventSource('/events');

  es.addEventListener('message', (e: MessageEvent) => {
    try {
      const snap = JSON.parse(e.data as string) as Snapshot;
      callbacks.onSnapshot(snap);
    } catch {
      callbacks.onError('Failed to parse SSE snapshot');
    }
  });

  // Named event type 'snapshot' — firmware may send this instead of default.
  es.addEventListener('snapshot', (e: MessageEvent) => {
    try {
      const snap = JSON.parse(e.data as string) as Snapshot;
      callbacks.onSnapshot(snap);
    } catch {
      callbacks.onError('Failed to parse SSE snapshot');
    }
  });

  es.addEventListener('error', () => {
    // readyState 2 = CLOSED; readyState 0 = CONNECTING (normal transient state)
    if (es.readyState === EventSource.CLOSED) {
      callbacks.onError('SSE connection closed');
    }
  });

  return {
    stop() {
      es.close();
    },
  };
}

// Re-export everything that other modules (config, calibration, device stores)
// import from this file, so they don't need to change their import paths.
export { temps, status, device, config, calibration, api, ApiError } from './client.ts';
export type {
  Snapshot,
  ChannelReading,
  StatusResponse,
  DeviceResponse,
  DeviceConfig,
  ChannelConfig,
  DeviceConfigPatch,
  ConfigStatus,
  SteinhartHartCoeffs,
  CalibrationLiveResponse,
  CalibrationSession,
  CalibrationPoint,
} from './client.ts';
