import { StyleSheet, Text, View } from 'react-native';

// Instrument-panel dashboard — implemented in Phase 9 (dashboard UX task).
export default function DashboardScreen() {
  return (
    <View style={styles.container}>
      <Text style={styles.placeholder}>Dashboard coming soon</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, alignItems: 'center', justifyContent: 'center' },
  placeholder: { fontSize: 16, color: '#888' },
});
