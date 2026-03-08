// Cook timer store — tracks per-channel cook start times.
//
// Usage:
//   cookStore.cookStartTime[channelId]   ← start timestamp ms | null
//   cookStore.startCook(channelId)       ← begin timing
//   cookStore.stopCook(channelId)        ← clear timer

class CookStore {
  cookStartTime = $state<Record<number, number | null>>({});

  startCook(channelId: number): void {
    this.cookStartTime = { ...this.cookStartTime, [channelId]: Date.now() };
  }

  stopCook(channelId: number): void {
    this.cookStartTime = { ...this.cookStartTime, [channelId]: null };
  }
}

export const cookStore = new CookStore();
