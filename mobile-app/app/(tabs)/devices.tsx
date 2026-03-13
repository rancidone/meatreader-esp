import { useCallback, useEffect, useState } from 'react';
import {
  Alert,
  FlatList,
  KeyboardAvoidingView,
  Platform,
  Pressable,
  StyleSheet,
  Text,
  TextInput,
  View,
} from 'react-native';
import type { SavedDevice } from '../../src/store/device';
import {
  addDevice,
  clearActiveIp,
  getActiveIp,
  loadDevices,
  removeDevice,
  setActiveIp,
} from '../../src/store/device';

// Basic IPv4 / hostname sanity check — rejects obviously bad input.
function isValidIp(value: string): boolean {
  return /^[\w.\-:]+$/.test(value.trim()) && value.trim().length > 0;
}

export default function DevicesScreen() {
  const [devices, setDevices] = useState<SavedDevice[]>([]);
  const [activeIp, setActiveIpState] = useState<string | null>(null);
  const [input, setInput] = useState('');
  const [adding, setAdding] = useState(false);

  const reload = useCallback(async () => {
    const [devs, ip] = await Promise.all([loadDevices(), getActiveIp()]);
    setDevices(devs);
    setActiveIpState(ip);
  }, []);

  useEffect(() => {
    reload();
  }, [reload]);

  async function handleAdd() {
    const ip = input.trim();
    if (!isValidIp(ip)) {
      Alert.alert('Invalid address', 'Enter a valid IP address or hostname.');
      return;
    }
    setAdding(true);
    try {
      const updated = await addDevice(ip);
      // Activate if this is the first device.
      if (updated.length === 1) {
        await setActiveIp(ip);
      }
      setInput('');
      await reload();
    } finally {
      setAdding(false);
    }
  }

  async function handleActivate(ip: string) {
    await setActiveIp(ip);
    setActiveIpState(ip);
  }

  async function handleRemove(ip: string) {
    Alert.alert('Remove device', `Remove ${ip}?`, [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Remove',
        style: 'destructive',
        onPress: async () => {
          const updated = await removeDevice(ip);
          // Clear active if we removed it; activate next if available.
          if (activeIp === ip) {
            const next = updated[0]?.ip ?? null;
            if (next) {
              await setActiveIp(next);
            } else {
              await clearActiveIp();
            }
          }
          await reload();
        },
      },
    ]);
  }

  return (
    <KeyboardAvoidingView
      style={styles.root}
      behavior={Platform.OS === 'ios' ? 'padding' : undefined}
    >
      {/* Add device form */}
      <View style={styles.addRow}>
        <TextInput
          style={styles.input}
          value={input}
          onChangeText={setInput}
          placeholder="192.168.1.100"
          placeholderTextColor="#aaa"
          autoCapitalize="none"
          autoCorrect={false}
          keyboardType="url"
          returnKeyType="done"
          onSubmitEditing={handleAdd}
          editable={!adding}
        />
        <Pressable
          style={({ pressed }) => [styles.addBtn, pressed && styles.addBtnPressed]}
          onPress={handleAdd}
          disabled={adding}
        >
          <Text style={styles.addBtnText}>{adding ? '…' : 'Add'}</Text>
        </Pressable>
      </View>

      {/* Saved devices list */}
      {devices.length === 0 ? (
        <View style={styles.empty}>
          <Text style={styles.emptyTitle}>No devices saved</Text>
          <Text style={styles.emptyBody}>Enter the IP address of your Meatreader above.</Text>
        </View>
      ) : (
        <FlatList
          data={devices}
          keyExtractor={(d) => d.ip}
          contentContainerStyle={styles.list}
          renderItem={({ item }) => {
            const isActive = item.ip === activeIp;
            return (
              <Pressable
                style={[styles.deviceRow, isActive && styles.deviceRowActive]}
                onPress={() => handleActivate(item.ip)}
              >
                <View style={styles.deviceInfo}>
                  {isActive && <View style={styles.activeDot} />}
                  <View>
                    <Text style={[styles.deviceIp, isActive && styles.deviceIpActive]}>
                      {item.ip}
                    </Text>
                    {item.label && (
                      <Text style={styles.deviceLabel}>{item.label}</Text>
                    )}
                    {item.lastSeenMs && (
                      <Text style={styles.deviceMeta}>
                        Last seen {new Date(item.lastSeenMs).toLocaleString()}
                      </Text>
                    )}
                  </View>
                </View>
                <Pressable
                  style={styles.removeBtn}
                  onPress={() => handleRemove(item.ip)}
                  hitSlop={8}
                >
                  <Text style={styles.removeBtnText}>Remove</Text>
                </Pressable>
              </Pressable>
            );
          }}
        />
      )}
    </KeyboardAvoidingView>
  );
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: '#f5f5f5' },

  addRow: {
    flexDirection: 'row',
    padding: 16,
    gap: 10,
    backgroundColor: '#fff',
    borderBottomWidth: StyleSheet.hairlineWidth,
    borderBottomColor: '#ddd',
  },
  input: {
    flex: 1,
    height: 44,
    borderWidth: 1,
    borderColor: '#ddd',
    borderRadius: 8,
    paddingHorizontal: 12,
    fontSize: 16,
    backgroundColor: '#fafafa',
  },
  addBtn: {
    height: 44,
    paddingHorizontal: 20,
    backgroundColor: '#c0392b',
    borderRadius: 8,
    alignItems: 'center',
    justifyContent: 'center',
  },
  addBtnPressed: { opacity: 0.75 },
  addBtnText: { color: '#fff', fontWeight: '700', fontSize: 16 },

  list: { padding: 16, gap: 10 },

  deviceRow: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    backgroundColor: '#fff',
    borderRadius: 10,
    padding: 14,
    shadowColor: '#000',
    shadowOpacity: 0.05,
    shadowRadius: 4,
    shadowOffset: { width: 0, height: 1 },
    elevation: 1,
  },
  deviceRowActive: {
    borderWidth: 2,
    borderColor: '#27ae60',
  },
  deviceInfo: { flexDirection: 'row', alignItems: 'center', gap: 10, flex: 1 },
  activeDot: {
    width: 10,
    height: 10,
    borderRadius: 5,
    backgroundColor: '#27ae60',
  },
  deviceIp: { fontSize: 16, fontWeight: '600', color: '#333' },
  deviceIpActive: { color: '#27ae60' },
  deviceLabel: { fontSize: 13, color: '#666', marginTop: 1 },
  deviceMeta: { fontSize: 11, color: '#aaa', marginTop: 2 },

  removeBtn: { paddingHorizontal: 10, paddingVertical: 4 },
  removeBtnText: { fontSize: 13, color: '#e74c3c', fontWeight: '600' },

  empty: { flex: 1, alignItems: 'center', justifyContent: 'center', padding: 32 },
  emptyTitle: { fontSize: 18, fontWeight: '700', color: '#333', marginBottom: 8 },
  emptyBody: { fontSize: 15, color: '#888', textAlign: 'center' },
});
