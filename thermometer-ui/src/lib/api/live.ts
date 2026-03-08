// Live data transport layer.
//
// Today: polling via client.ts.  All stores import from here, not from
// client.ts directly, so SSE can be wired in later without touching stores.
//
// Future SSE migration sketch:
//
//   const es = new EventSource('/events');
//   es.addEventListener('snapshot', (e) => {
//     const snap = JSON.parse(e.data) as Snapshot;
//     onSnapshot(snap);
//   });
//
// When SSE is added, replace the polling exports below with SSE-backed ones
// that have the same interface.

// ApiError is a class (value), not a type-only export.
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
