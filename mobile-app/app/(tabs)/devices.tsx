import { StyleSheet, Text, View } from 'react-native';

// Device onboarding / saved device list — implemented in Phase 9 (onboarding flow task).
export default function DevicesScreen() {
  return (
    <View style={styles.container}>
      <Text style={styles.placeholder}>Device management coming soon</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, alignItems: 'center', justifyContent: 'center' },
  placeholder: { fontSize: 16, color: '#888' },
});
