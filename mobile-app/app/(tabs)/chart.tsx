// Simple temperature history chart tab.
// Accumulates a rolling window of snapshots from /temps/latest and renders
// per-channel sparklines using pure React Native layout (no SVG library).

import type { Snapshot } from '@meatreader/api-types';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Dimensions, ScrollView, StyleSheet, Text, View } from 'react-native';
import { MeatreaderClient } from '../../src/api/client';
import { getActiveIp } from '../../src/store/device';

const CHANNEL_COLORS = ['#e67e22', '#2980b9'] as const;
const MAX_HISTORY = 120; // 4 minutes at 2 s poll
const POLL_MS = 2000;
const CHART_HEIGHT = 140;

function Sparkline({
  values,
  color,
  minTemp,
  maxTemp,
  width,
}: {
  values: number[];
  color: string;
  minTemp: number;
  maxTemp: number;
  width: number;
}) {
  if (values.length < 2) return null;
  const range = maxTemp - minTemp || 1;
  const segW = width / (values.length - 1);

  const segments: { x: number; y: number; length: number; angle: number }[] = [];

  for (let i = 0; i < values.length - 1; i++) {
    const x1 = i * segW;
    const x2 = (i + 1) * segW;
    const y1 = CHART_HEIGHT - ((values[i] - minTemp) / range) * CHART_HEIGHT;
    const y2 = CHART_HEIGHT - ((values[i + 1] - minTemp) / range) * CHART_HEIGHT;

    const dx = x2 - x1;
    const dy = y2 - y1;
    const length = Math.sqrt(dx * dx + dy * dy);
    const angle = (Math.atan2(dy, dx) * 180) / Math.PI;

    segments.push({ x: x1, y: y1, length, angle });
  }

  return (
    <View style={{ position: 'absolute', top: 0, left: 0, width, height: CHART_HEIGHT }}>
      {segments.map((seg, i) => (
        <View
          key={i}
          style={{
            position: 'absolute',
            left: seg.x,
            top: seg.y - 1,
            width: seg.length,
            height: 2.5,
            backgroundColor: color,
            transformOrigin: 'left center',
            transform: [{ rotate: `${seg.angle}deg` }],
          }}
        />
      ))}
    </View>
  );
}

export default function ChartScreen() {
  const [deviceIp, setDeviceIp] = useState<string | null>(null);
  const [history, setHistory] = useState<Snapshot[]>([]);
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const clientRef = useRef<MeatreaderClient | null>(null);

  useEffect(() => {
    getActiveIp().then(setDeviceIp);
  }, []);

  useEffect(() => {
    if (!deviceIp) return;
    clientRef.current = new MeatreaderClient(deviceIp);

    const poll = () => {
      clientRef.current
        ?.getLatest()
        .then((snap) => {
          setHistory((prev) => {
            const next = [...prev, snap];
            return next.length > MAX_HISTORY ? next.slice(next.length - MAX_HISTORY) : next;
          });
        })
        .catch(() => {})
        .finally(() => {
          timerRef.current = setTimeout(poll, POLL_MS);
        });
    };

    poll();
    return () => {
      if (timerRef.current) clearTimeout(timerRef.current);
    };
  }, [deviceIp]);

  const { width: screenWidth } = Dimensions.get('window');
  const chartWidth = screenWidth - 32; // 16px padding each side

  // Per-channel temperature arrays (only ok/stale quality points)
  const channelSeries = useMemo(() => {
    const series: (number | null)[][] = [[], []];
    for (const snap of history) {
      for (let ch = 0; ch < 2; ch++) {
        const r = snap.channels[ch];
        if (r && (r.quality === 'ok' || r.quality === 'stale')) {
          series[ch].push(r.temperature_c);
        } else {
          series[ch].push(null);
        }
      }
    }
    return series;
  }, [history]);

  const allTemps = channelSeries.flatMap((s) => s.filter((v): v is number => v !== null));
  const minTemp = allTemps.length ? Math.floor(Math.min(...allTemps)) - 2 : 0;
  const maxTemp = allTemps.length ? Math.ceil(Math.max(...allTemps)) + 2 : 100;

  if (!deviceIp) {
    return (
      <View style={styles.centered}>
        <Text style={styles.emptyTitle}>No device connected</Text>
        <Text style={styles.emptyBody}>Go to Devices tab to add one.</Text>
      </View>
    );
  }

  // Latest readings for the legend
  const latest = history[history.length - 1];

  return (
    <ScrollView contentContainerStyle={styles.scroll}>
      <Text style={styles.heading}>Temperature History</Text>
      <Text style={styles.sub}>Last {history.length} readings · {POLL_MS / 1000} s interval</Text>

      {/* Chart area */}
      <View style={[styles.chartBox, { height: CHART_HEIGHT + 32 }]}>
        {/* Y-axis labels */}
        <Text style={[styles.axisLabel, { top: 0 }]}>{maxTemp}°</Text>
        <Text style={[styles.axisLabel, { bottom: 16 }]}>{minTemp}°</Text>

        {/* Plot area */}
        <View style={{ marginLeft: 28, height: CHART_HEIGHT, position: 'relative' }}>
          {channelSeries.map((series, ch) => {
            const dense = series.filter((v): v is number => v !== null);
            return (
              <Sparkline
                key={ch}
                values={dense}
                color={CHANNEL_COLORS[ch % CHANNEL_COLORS.length]}
                minTemp={minTemp}
                maxTemp={maxTemp}
                width={chartWidth - 28}
              />
            );
          })}
        </View>
      </View>

      {/* Legend */}
      {latest && (
        <View style={styles.legend}>
          {latest.channels.map((ch) => {
            if (ch.quality === 'disabled') return null;
            return (
              <View key={ch.id} style={styles.legendItem}>
                <View
                  style={[
                    styles.legendDot,
                    { backgroundColor: CHANNEL_COLORS[ch.id % CHANNEL_COLORS.length] },
                  ]}
                />
                <Text style={styles.legendLabel}>CH {ch.id + 1}</Text>
                {ch.quality === 'ok' || ch.quality === 'stale' ? (
                  <Text style={styles.legendTemp}>{ch.temperature_c.toFixed(1)} °C</Text>
                ) : (
                  <Text style={[styles.legendTemp, { color: '#e74c3c' }]}>{ch.quality}</Text>
                )}
              </View>
            );
          })}
        </View>
      )}

      {history.length === 0 && (
        <View style={styles.centered}>
          <Text style={styles.emptyBody}>Waiting for first reading…</Text>
        </View>
      )}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  scroll: { flexGrow: 1, padding: 16, backgroundColor: '#f5f5f5' },
  centered: { flex: 1, alignItems: 'center', justifyContent: 'center', padding: 32 },
  heading: { fontSize: 18, fontWeight: '700', color: '#1a1a1a', marginBottom: 4 },
  sub: { fontSize: 12, color: '#888', marginBottom: 16 },

  chartBox: {
    backgroundColor: '#fff',
    borderRadius: 12,
    padding: 8,
    paddingBottom: 16,
    shadowColor: '#000',
    shadowOpacity: 0.06,
    shadowRadius: 6,
    shadowOffset: { width: 0, height: 2 },
    elevation: 2,
    marginBottom: 16,
    position: 'relative',
  },
  axisLabel: {
    position: 'absolute',
    left: 2,
    fontSize: 10,
    color: '#aaa',
    width: 24,
    textAlign: 'right',
  },

  legend: {
    backgroundColor: '#fff',
    borderRadius: 12,
    padding: 14,
    gap: 10,
    shadowColor: '#000',
    shadowOpacity: 0.06,
    shadowRadius: 6,
    shadowOffset: { width: 0, height: 2 },
    elevation: 2,
  },
  legendItem: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  legendDot: { width: 10, height: 10, borderRadius: 5 },
  legendLabel: { fontSize: 14, fontWeight: '600', color: '#555', width: 40 },
  legendTemp: { fontSize: 22, fontWeight: '700', color: '#1a1a1a' },

  emptyTitle: { fontSize: 18, fontWeight: '700', marginBottom: 8, color: '#333' },
  emptyBody: { fontSize: 15, color: '#888', textAlign: 'center' },
});
