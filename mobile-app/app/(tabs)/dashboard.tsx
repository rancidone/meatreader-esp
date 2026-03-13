import type { ChannelReading, Quality } from '@meatreader/api-types';
import { useEffect, useMemo, useState } from 'react';
import { ScrollView, StyleSheet, Text, View } from 'react-native';
import { MeatreaderClient } from '../../src/api/client';
import { usePolling } from '../../src/api/usePolling';
import { getActiveIp } from '../../src/store/device';

const QUALITY_COLOR: Record<Quality, string> = {
  ok: '#27ae60',
  stale: '#e67e22',
  disabled: '#95a5a6',
  error: '#e74c3c',
};

function ChannelTile({ ch }: { ch: ChannelReading }) {
  const qualityColor = QUALITY_COLOR[ch.quality];
  const alerting = ch.alert_triggered === true;

  return (
    <View style={[styles.tile, alerting && styles.tileAlert]}>
      <Text style={styles.tileLabel}>CH {ch.id + 1}</Text>
      {ch.quality === 'ok' || ch.quality === 'stale' ? (
        <Text style={styles.tileTemp}>
          {ch.temperature_c.toFixed(1)}
          <Text style={styles.tileTempUnit}> °C</Text>
        </Text>
      ) : (
        <Text style={[styles.tileTemp, { color: qualityColor }]}>
          {ch.quality.toUpperCase()}
        </Text>
      )}
      <View style={[styles.qualityDot, { backgroundColor: qualityColor }]} />
      {alerting && <Text style={styles.alertBadge}>ALERT</Text>}
      {ch.stall_detected && <Text style={styles.stallBadge}>STALL</Text>}
    </View>
  );
}

export default function DashboardScreen() {
  const [deviceIp, setDeviceIp] = useState<string | null>(null);

  useEffect(() => {
    getActiveIp().then(setDeviceIp);
  }, []);

  const client = useMemo(
    () => (deviceIp ? new MeatreaderClient(deviceIp) : null),
    [deviceIp],
  );

  const { data: snapshot, error, loading } = usePolling(
    () => client?.getLatest() ?? Promise.resolve(null),
    2000,
  );

  if (!deviceIp) {
    return (
      <View style={styles.centered}>
        <Text style={styles.emptyTitle}>No device connected</Text>
        <Text style={styles.emptyBody}>Go to Devices tab to add one.</Text>
      </View>
    );
  }

  const activeChannels = snapshot?.channels.filter((ch) => ch.quality !== 'disabled') ?? [];

  return (
    <ScrollView contentContainerStyle={styles.scroll}>
      {/* Connection banner */}
      <View style={[styles.banner, error ? styles.bannerError : styles.bannerOk]}>
        <Text style={styles.bannerText}>
          {error
            ? `Disconnected · ${deviceIp}`
            : loading && !snapshot
              ? `Connecting to ${deviceIp}…`
              : `Live · ${deviceIp}`}
        </Text>
      </View>

      {/* Channel grid */}
      {activeChannels.length > 0 ? (
        <View style={styles.grid}>
          {activeChannels.map((ch) => (
            <ChannelTile key={ch.id} ch={ch} />
          ))}
        </View>
      ) : (
        !loading && (
          <View style={styles.centered}>
            <Text style={styles.emptyBody}>No active channels</Text>
          </View>
        )
      )}

      {/* Last updated */}
      {snapshot && (
        <Text style={styles.timestamp}>
          Updated {new Date(snapshot.timestamp).toLocaleTimeString()}
        </Text>
      )}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  scroll: { flexGrow: 1, padding: 16, backgroundColor: '#f5f5f5' },
  centered: { flex: 1, alignItems: 'center', justifyContent: 'center', padding: 32 },

  banner: {
    borderRadius: 8,
    paddingVertical: 8,
    paddingHorizontal: 14,
    marginBottom: 16,
    alignItems: 'center',
  },
  bannerOk: { backgroundColor: '#eafaf1' },
  bannerError: { backgroundColor: '#fdecea' },
  bannerText: { fontSize: 13, fontWeight: '600' },

  grid: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 12,
    justifyContent: 'flex-start',
  },
  tile: {
    width: '47%',
    backgroundColor: '#fff',
    borderRadius: 12,
    padding: 18,
    shadowColor: '#000',
    shadowOpacity: 0.07,
    shadowRadius: 6,
    shadowOffset: { width: 0, height: 2 },
    elevation: 2,
    position: 'relative',
  },
  tileAlert: {
    borderWidth: 2,
    borderColor: '#e74c3c',
  },
  tileLabel: { fontSize: 13, color: '#888', marginBottom: 4, fontWeight: '600' },
  tileTemp: { fontSize: 40, fontWeight: '700', color: '#1a1a1a', lineHeight: 48 },
  tileTempUnit: { fontSize: 20, fontWeight: '400', color: '#555' },
  qualityDot: { width: 8, height: 8, borderRadius: 4, marginTop: 8 },
  alertBadge: {
    position: 'absolute',
    top: 10,
    right: 10,
    backgroundColor: '#e74c3c',
    color: '#fff',
    fontSize: 10,
    fontWeight: '700',
    paddingHorizontal: 6,
    paddingVertical: 2,
    borderRadius: 4,
    overflow: 'hidden',
  },
  stallBadge: {
    position: 'absolute',
    top: 10,
    right: 10,
    backgroundColor: '#e67e22',
    color: '#fff',
    fontSize: 10,
    fontWeight: '700',
    paddingHorizontal: 6,
    paddingVertical: 2,
    borderRadius: 4,
    overflow: 'hidden',
  },

  emptyTitle: { fontSize: 18, fontWeight: '700', marginBottom: 8, color: '#333' },
  emptyBody: { fontSize: 15, color: '#888', textAlign: 'center' },
  timestamp: { marginTop: 16, fontSize: 12, color: '#aaa', textAlign: 'center' },
});
