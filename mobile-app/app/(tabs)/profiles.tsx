import { StyleSheet, Text, View } from 'react-native';

// Cook profiles — implemented in Phase 9 (mobile actions task).
export default function ProfilesScreen() {
  return (
    <View style={styles.container}>
      <Text style={styles.placeholder}>Cook profiles coming soon</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, alignItems: 'center', justifyContent: 'center' },
  placeholder: { fontSize: 16, color: '#888' },
});
