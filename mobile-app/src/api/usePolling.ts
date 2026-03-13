import { useCallback, useEffect, useRef, useState } from 'react';

export interface PollState<T> {
  data: T | null;
  error: Error | null;
  loading: boolean;
}

/**
 * Polls `fetcher` every `intervalMs` while the component is mounted.
 * Retries on error without resetting data, so stale data stays visible.
 */
export function usePolling<T>(
  fetcher: () => Promise<T>,
  intervalMs: number = 2000,
): PollState<T> {
  const [state, setState] = useState<PollState<T>>({
    data: null,
    error: null,
    loading: true,
  });

  const fetcherRef = useRef(fetcher);
  fetcherRef.current = fetcher;

  const run = useCallback(async () => {
    try {
      const data = await fetcherRef.current();
      setState({ data, error: null, loading: false });
    } catch (err) {
      setState((prev) => ({
        data: prev.data,
        error: err instanceof Error ? err : new Error(String(err)),
        loading: false,
      }));
    }
  }, []);

  useEffect(() => {
    let active = true;

    const tick = async () => {
      if (!active) return;
      await run();
      if (active) {
        // eslint-disable-next-line @typescript-eslint/no-use-before-define
        timer = setTimeout(tick, intervalMs);
      }
    };

    let timer = setTimeout(tick, 0);
    return () => {
      active = false;
      clearTimeout(timer);
    };
  }, [run, intervalMs]);

  return state;
}
