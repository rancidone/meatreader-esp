import type { AlertConfig, AlertMethod, CookProfile } from '@meatreader/api-types';
import { useEffect, useMemo, useState } from 'react';
import { ActivityIndicator, Alert, FlatList, Pressable, StyleSheet, Text, View } from 'react-native';
import { MeatreaderClient } from '../../src/api/client';
import { getActiveIp } from '../../src/store/device';

const NUM_CHANNELS = 4;

// Map a profile's stages onto a full AlertConfig array (one entry per channel).
// Channels beyond the profile's stage count are disabled.
function profileToAlerts(profile: CookProfile): AlertConfig[] {
  return Array.from({ length: NUM_CHANNELS }, (_, i) => {
    const stage = profile.stages[i];
    if (!stage) {
      return { enabled: false, target_temp_c: 0, method: 'none' as AlertMethod, webhook_url: '' };
    }
    return {
      enabled: true,
      target_temp_c: stage.target_temp_c,
      method: stage.alert_method as AlertMethod,
      webhook_url: '',
    };
  });
}

function ProfileCard({
  profile,
  onApply,
  applying,
}: {
  profile: CookProfile;
  onApply: () => void;
  applying: boolean;
}) {
  return (
    <View style={styles.card}>
      <View style={styles.cardHeader}>
        <Text style={styles.profileName}>{profile.name}</Text>
        <Pressable
          style={({ pressed }) => [styles.applyBtn, pressed && styles.applyBtnPressed, applying && styles.applyBtnDisabled]}
          onPress={onApply}
          disabled={applying}
        >
          <Text style={styles.applyBtnText}>{applying ? 'Applying…' : 'Apply'}</Text>
        </Pressable>
      </View>
      {profile.stages.map((stage, i) => (
        <View key={i} style={styles.stageRow}>
          <Text style={styles.stageIndex}>Stage {i + 1}</Text>
          <Text style={styles.stageLabel}>{stage.label}</Text>
          <Text style={styles.stageTemp}>{stage.target_temp_c.toFixed(0)} °C</Text>
        </View>
      ))}
    </View>
  );
}

export default function ProfilesScreen() {
  const [deviceIp, setDeviceIp] = useState<string | null>(null);
  const [profiles, setProfiles] = useState<CookProfile[]>([]);
  const [loading, setLoading] = useState(true);
  const [applyingId, setApplyingId] = useState<number | null>(null);
  const [error, setError] = useState<string | null>(null);

  const client = useMemo(() => (deviceIp ? new MeatreaderClient(deviceIp) : null), [deviceIp]);

  useEffect(() => {
    getActiveIp().then(setDeviceIp);
  }, []);

  useEffect(() => {
    if (!client) { setLoading(false); return; }
    client.getProfiles()
      .then((all) => setProfiles(all.filter((p) => p.name !== '')))
      .catch((e: unknown) => setError(e instanceof Error ? e.message : 'Failed to load profiles'))
      .finally(() => setLoading(false));
  }, [client]);

  async function handleApply(profile: CookProfile, index: number) {
    if (!client) return;
    Alert.alert('Apply profile', `Apply "${profile.name}"? This will update alert targets on all channels.`, [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Apply',
        onPress: async () => {
          setApplyingId(index);
          try {
            await client.patchAlerts(profileToAlerts(profile));
            await client.postConfigApply();
            Alert.alert('Done', `"${profile.name}" is now active.`);
          } catch (e: unknown) {
            Alert.alert('Error', e instanceof Error ? e.message : 'Apply failed');
          } finally {
            setApplyingId(null);
          }
        },
      },
    ]);
  }

  if (!deviceIp) {
    return (
      <View style={styles.centered}>
        <Text style={styles.emptyTitle}>No device connected</Text>
        <Text style={styles.emptyBody}>Go to Devices tab to add one.</Text>
      </View>
    );
  }

  if (loading) {
    return <View style={styles.centered}><ActivityIndicator size="large" /></View>;
  }

  if (error) {
    return (
      <View style={styles.centered}>
        <Text style={styles.errorText}>{error}</Text>
      </View>
    );
  }

  return (
    <FlatList
      data={profiles}
      keyExtractor={(p, i) => `${i}-${p.name}`}
      contentContainerStyle={profiles.length === 0 ? styles.centered : styles.list}
      ListEmptyComponent={
        <View style={styles.centered}>
          <Text style={styles.emptyTitle}>No profiles saved</Text>
          <Text style={styles.emptyBody}>Create profiles in the web admin console.</Text>
        </View>
      }
      renderItem={({ item, index }) => (
        <ProfileCard
          profile={item}
          onApply={() => handleApply(item, index)}
          applying={applyingId === index}
        />
      )}
    />
  );
}

const styles = StyleSheet.create({
  list: { padding: 16, gap: 12 },
  centered: { flex: 1, alignItems: 'center', justifyContent: 'center', padding: 32 },

  card: {
    backgroundColor: '#fff',
    borderRadius: 12,
    padding: 16,
    shadowColor: '#000',
    shadowOpacity: 0.06,
    shadowRadius: 5,
    shadowOffset: { width: 0, height: 2 },
    elevation: 2,
  },
  cardHeader: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 },
  profileName: { fontSize: 17, fontWeight: '700', color: '#1a1a1a', flex: 1 },

  applyBtn: { backgroundColor: '#c0392b', borderRadius: 8, paddingHorizontal: 16, paddingVertical: 8 },
  applyBtnPressed: { opacity: 0.75 },
  applyBtnDisabled: { opacity: 0.5 },
  applyBtnText: { color: '#fff', fontWeight: '700', fontSize: 14 },

  stageRow: { flexDirection: 'row', alignItems: 'center', paddingVertical: 4, gap: 8 },
  stageIndex: { fontSize: 12, color: '#aaa', width: 50 },
  stageLabel: { fontSize: 14, color: '#555', flex: 1 },
  stageTemp: { fontSize: 14, fontWeight: '600', color: '#1a1a1a' },

  emptyTitle: { fontSize: 18, fontWeight: '700', color: '#333', marginBottom: 8 },
  emptyBody: { fontSize: 15, color: '#888', textAlign: 'center' },
  errorText: { fontSize: 15, color: '#e74c3c', textAlign: 'center' },
});
