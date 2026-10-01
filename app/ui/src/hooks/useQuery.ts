// A live engine query: re-runs when `key` changes, at most one request in flight, the latest key wins, and
// runs at most every `minIntervalMs` (for queries driven by playback).
import { useEffect, useRef, useState } from 'react';
import { errorOf } from '../api/engine';
import type { EngineErrorInfo } from '../api/types';

export interface QueryState<T> {
  data?: T;
  error?: EngineErrorInfo;
  loading: boolean;
}

export function useQuery<T>(key: string | null, run: () => Promise<T>, minIntervalMs = 0): QueryState<T> {
  const [state, setState] = useState<QueryState<T>>({ loading: key !== null });
  const runRef = useRef(run);
  runRef.current = run;
  const inFlight = useRef(false);
  const pendingKey = useRef<string | null>(null);
  const lastStart = useRef(0);
  const timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const currentKey = useRef<string | null>(key);
  currentKey.current = key;

  useEffect(() => {
    if (key === null) {
      setState({ loading: false });
      return;
    }
    pendingKey.current = key;
    const start = () => {
      if (inFlight.current || pendingKey.current === null) return;
      const wait = lastStart.current + minIntervalMs - performance.now();
      if (wait > 0) {
        clearTimeout(timer.current);
        timer.current = setTimeout(start, wait);
        return;
      }
      const k = pendingKey.current;
      pendingKey.current = null;
      inFlight.current = true;
      lastStart.current = performance.now();
      setState((s) => ({ ...s, loading: true }));
      runRef
        .current()
        .then(
          (data) => {
            if (currentKey.current !== null) setState({ data, loading: false });
          },
          (e) => {
            if (currentKey.current !== null) setState((s) => ({ data: s.data, error: errorOf(e), loading: false }));
          },
        )
        .finally(() => {
          inFlight.current = false;
          if (pendingKey.current !== null && pendingKey.current !== k) start();
        });
    };
    start();
    return () => clearTimeout(timer.current);
  }, [key, minIntervalMs]);

  return state;
}
