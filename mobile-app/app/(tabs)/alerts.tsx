import type { AlertStatus, ChannelReading, Snapshot } from '@meatreader/api-types';
import { useEffect, useMemo, useState } from 'react';
import { ScrollView, StyleSheet, Text, View } from 'react-native';
import { MeatreaderClient } from '../../src/api/client';
import { usePolling } from '../../src/api/usePolling';
import { getActiveIp } from '../../src/store/device';

function AlertRow({ ch, config }: { ch: ChannelReading; config?: AlertStatus }) {
  return (
    <View style={styles.alertRow}>
      <View style={styles.alertDot} />
      <View style={styles.alertInfo}>
        <Text style={styles.alertChannel}>Channel {ch.id + 1}</Text>
        <Text style={styles.alertDetail}>
          {ch.temperature_c.toFixed(1)} °C
          {config ? ` · target ${config.target_temp_c.toFixed(0)} °C` : ''}
        </Text>
        {ch.stall_detected && <Text style={styles.stallNote}>Stall detected</Text>}
      </View>
    </View>
  );
}

type PollResult = { snapshot: Snapshot; alerts: AlertStatus[] } | null;

export default function AlertsScreen() {
  const [deviceIp, setDeviceIp] = useState<string | null>(null);

  useEffect(() => {
    getActiveIp().then(setDeviceIp);
  }, []);

  const client = useMemo(() => (deviceIp ? new MeatreaderClient(deviceIp) : null), [deviceIp]);

  const { data, error } = usePolling<PollResult>(
    async () => {
      if (!client) return null;
      const [snapshot, alerts] = await Promise.all([client.getLatest(), client.getAlerts()]);
      return { snapshot, alerts };
    },
    3000,
  );

  if (!deviceIp) {
    return (
      <View style={styles.centered}>
        <Text style={styles.emptyTitle}>No device connected</Text>
        <Text style={styles.emptyBody}>Go to Devices tab to add one.</Text>
      </View>
    );
  }

  const triggeredChannels = data?.snapshot.channels.filter((ch) => ch.alert_triggered) ?? [];
  const stalledChannels = data?.snapshot.channels.filter((ch) => ch.stall_detected && !ch.alert_triggered) ?? [];

  return (
    <ScrollView contentContainerStyle={styles.scroll}>
      {/* Status banner */}
      <View style={[styles.banner, triggeredChannels.length > 0 ? styles.bannerAlert : styles.bannerClear]}>
        <Text style={styles.bannerText}>
          {error
            ? 'Disconnected'
            : triggeredChannels.length > 0
              ? `${triggeredChannels.length} alert${triggeredChannels.length > 1 ? 's' : ''} active`
              : 'No alerts — all clear'}
        </Text>
      </View>

      {/* Triggered alerts */}
      {triggeredChannels.length > 0 && (
        <View style={styles.section}>
          <Text style={styles.sectionTitle}>ALERT</Text>
          {triggeredChannels.map((ch) => (
            <AlertRow
              key={ch.id}
              ch={ch}
              config={data?.alerts[ch.id]}
            />
          ))}
        </View>
      )}

      {/* Stalls (warning, not full alert) */}
      {stalledChannels.length > 0 && (
        <View style={styles.section}>
          <Text style={[styles.sectionTitle, styles.sectionTitleStall]}>STALL DETECTED</Text>
          {stalledChannels.map((ch) => (
            <AlertRow key={ch.id} ch={ch} config={data?.alerts[ch.id]} />
          ))}
        </View>
      )}

      {triggeredChannels.length === 0 && stalledChannels.length === 0 && data && (
        <View style={styles.centered}>
          <Text style={styles.clearText}>Nothing to report</Text>
        </View>
      )}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  scroll: { flexGrow: 1, padding: 16, backgroundColor: '#f5f5f5' },
  centered: { flex: 1, alignItems: 'center', justifyContent: 'center', padding: 32 },

  banner: {
    borderRadius: 8,
    paddingVertical: 10,
    paddingHorizontal: 14,
    marginBottom: 16,
    alignItems: 'center',
  },
  bannerAlert: { backgroundColor: '#fdecea' },
  bannerClear: { backgroundColor: '#eafaf1' },
  bannerText: { fontSize: 14, fontWeight: '700' },

  section: { marginBottom: 16 },
  sectionTitle: {
    fontSize: 11,
    fontWeight: '700',
    color: '#e74c3c',
    letterSpacing: 1,
    marginBottom: 8,
  },
  sectionTitleStall: { color: '#e67e22' },

  alertRow: {
    flexDirection: 'row',
    alignItems: 'flex-start',
    backgroundColor: '#fff',
    borderRadius: 10,
    padding: 14,
    marginBottom: 8,
    gap: 12,
    shadowColor: '#000',
    shadowOpacity: 0.05,
    shadowRadius: 4,
    shadowOffset: { width: 0, height: 1 },
    elevation: 1,
  },
  alertDot: { width: 10, height: 10, borderRadius: 5, backgroundColor: '#e74c3c', marginTop: 4 },
  alertInfo: { flex: 1 },
  alertChannel: { fontSize: 15, fontWeight: '700', color: '#1a1a1a' },
  alertDetail: { fontSize: 13, color: '#555', marginTop: 2 },
  stallNote: { fontSize: 12, color: '#e67e22', marginTop: 2, fontWeight: '600' },

  emptyTitle: { fontSize: 18, fontWeight: '700', color: '#333', marginBottom: 8 },
  emptyBody: { fontSize: 15, color: '#888', textAlign: 'center' },
  clearText: { fontSize: 16, color: '#888' },
});
