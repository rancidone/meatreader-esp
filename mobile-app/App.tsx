import { StatusBar } from 'expo-status-bar';
import { useEffect, useMemo, useState } from 'react';
import { StyleSheet, Text, View } from 'react-native';
import { MeatreaderClient } from './src/api/client';
import { usePolling } from './src/api/usePolling';
import { getActiveIp } from './src/store/device';

// Placeholder root — replaced by the navigation shell in Phase 9.
export default function App() {
  const [deviceIp, setDeviceIp] = useState<string | null>(null);

  useEffect(() => {
    getActiveIp().then(setDeviceIp);
  }, []);

  const client = useMemo(
    () => (deviceIp ? new MeatreaderClient(deviceIp) : null),
    [deviceIp],
  );

  const { data: snapshot, error } = usePolling(
    () => client?.getLatest() ?? Promise.resolve(null),
    2000,
  );

  return (
    <View style={styles.container}>
      {deviceIp ? (
        <>
          <Text style={styles.label}>Device: {deviceIp}</Text>
          {error && <Text style={styles.error}>Connection error</Text>}
          {snapshot && (
            <Text style={styles.label}>
              {snapshot.channels.length} channel(s) — last update {new Date(snapshot.timestamp).toLocaleTimeString()}
            </Text>
          )}
        </>
      ) : (
        <Text style={styles.label}>No device connected. Add one to get started.</Text>
      )}
      <StatusBar style="auto" />
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#fff',
    alignItems: 'center',
    justifyContent: 'center',
    padding: 24,
  },
  label: {
    fontSize: 16,
    marginBottom: 8,
    textAlign: 'center',
  },
  error: {
    fontSize: 14,
    color: '#cc0000',
    marginBottom: 8,
  },
});
