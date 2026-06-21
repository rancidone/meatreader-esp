export type LogLevel = 'info' | 'warn' | 'error';

export interface LogEntry {
  id:      number;
  ts:      number;   // Date.now()
  level:   LogLevel;
  message: string;
}

const MAX_ENTRIES = 500;
let nextId = 0;

class ConsoleLogStore {
  entries = $state<LogEntry[]>([]);

  push(level: LogLevel, message: string): void {
    const entry: LogEntry = { id: nextId++, ts: Date.now(), level, message };
    const next = [...this.entries, entry];
    this.entries = next.length > MAX_ENTRIES ? next.slice(next.length - MAX_ENTRIES) : next;
  }

  clear(): void {
    this.entries = [];
  }
}

export const consoleLog = new ConsoleLogStore();
