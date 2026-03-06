// UI preferences — unit toggle, global polling switch, history buffer size.

export type TempUnit = 'C' | 'F';

class UiStore {
  unit        = $state<TempUnit>('F');
  polling     = $state(true);
  historySize = $state(300);   // seconds at 1 Hz; default 5 min

  toggleUnit() {
    this.unit = this.unit === 'F' ? 'C' : 'F';
  }

  togglePolling() {
    this.polling = !this.polling;
  }

  setHistorySize(n: number) {
    this.historySize = n;
  }
}

export const ui = new UiStore();
