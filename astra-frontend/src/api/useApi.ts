/**
 * useApi — the single polling data-fetch hook.
 *
 * Guarantees (§20 of the build mission):
 *  - centralized logic; no per-component fetch
 *  - automatic polling with an interval; no overlapping requests to the same
 *    endpoint (a tick is skipped while a request is in flight)
 *  - obsolete requests cancelled via AbortController
 *  - old data never looks current: if a poll fails, `stale` becomes true and
 *    the UI keeps showing the previous data with its real fetch time
 *  - background polling pauses when the document is hidden
 *  - refetch() for manual retry
 */
import { useCallback, useEffect, useRef, useState } from "react";
import { friendlyError } from "./client";

export interface UseApiResult<T> {
  data: T | null;
  loading: boolean;
  stale: boolean;
  error: string | null;
  errorDetail: string | null;
  /** epoch ms of last successful fetch (null = never) */
  fetchedAt: number | null;
  refetch: () => void;
}

interface UseApiOptions<T> {
  fetcher: (signal: AbortSignal) => Promise<T>;
  /** Poll interval in ms. 0 = fetch once, no polling. */
  intervalMs?: number;
  /** Skip fetching entirely (e.g. unconfigured base URL). */
  enabled?: boolean;
  /** Keep the previous data on error (default true). */
  keepPreviousOnError?: boolean;
  deps?: unknown[];
}

export function useApi<T>(opts: UseApiOptions<T>): UseApiResult<T> {
  const {
    fetcher,
    intervalMs = 0,
    enabled = true,
    keepPreviousOnError = true,
    deps = [],
  } = opts;

  const [data, setData] = useState<T | null>(null);
  // `loading` is the *first-load* spinner: true until the first settle
  // (data or error). Later polls resolve silently behind rendered data, so the
  // screen never flashes back to a skeleton once it has real content.
  const [loading, setLoading] = useState<boolean>(enabled);
  const [stale, setStale] = useState<boolean>(false);
  const [error, setError] = useState<string | null>(null);
  const [errorDetail, setErrorDetail] = useState<string | null>(null);
  const [fetchedAt, setFetchedAt] = useState<number | null>(null);
  const [tickCounter, setTick] = useState(0);

  const inFlight = useRef<AbortController | null>(null);
  const mounted = useRef(true);
  const dataRef = useRef<T | null>(null);

  // Track the latest data for effects that must not depend on it directly.
  useEffect(() => {
    dataRef.current = data;
  }, [data]);
  const lastFetched = useRef<number | null>(null);
  const depsRef = useRef(deps);
  // `deps` is a fresh array literal on every render, so keep its *values* in the
  // ref and spread them into the effect's dependency list below. Without this
  // the list stays frozen at its first-render values and a screen that refetches
  // on a selection change (Chart's timeframe control) never re-fetches.
  depsRef.current = deps;

  // Callers pass an inline `fetcher` arrow on every render, so its identity
  // changes each time. Reading it through a ref keeps `run` stable; otherwise
  // `run` changes identity every render, the fetch effect below re-fires, its
  // setState re-renders, and the screen polls at network speed instead of at
  // `intervalMs`.
  const fetcherRef = useRef(fetcher);
  fetcherRef.current = fetcher;

  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
      inFlight.current?.abort();
    };
  }, []);

  const run = useCallback(() => {
    if (!enabled) return;
    // Never overlap: skip this tick when a request is still in flight.
    if (inFlight.current !== null) return;
    const ctrl = new AbortController();
    inFlight.current = ctrl;

    fetcherRef.current(ctrl.signal)
      .then((result) => {
        if (!mounted.current || ctrl.signal.aborted) return;
        setData(result);
        setError(null);
        setErrorDetail(null);
        setStale(false);
        lastFetched.current = Date.now();
        setFetchedAt(lastFetched.current);
      })
      .catch((err: unknown) => {
        if (!mounted.current || ctrl.signal.aborted) return;
        const fe = friendlyError(err);
        setError(fe.title);
        setErrorDetail(fe.detail);
        if (!keepPreviousOnError) setData(null);
        // Old data must not look current.
        setStale(true);
      })
      .finally(() => {
        if (mounted.current) setLoading(false);
      })
      .finally(() => {
        if (inFlight.current === ctrl) inFlight.current = null;
      });
  }, [enabled, keepPreviousOnError]);

  // A disabled→enabled transition (e.g. the base URL becomes configured)
  // re-arms the first-load spinner.
  useEffect(() => {
    if (enabled && dataRef.current === null) setLoading(true);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [enabled]);

  // Initial + dep-triggered fetch.
  useEffect(() => {
    run();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [run, tickCounter, ...depsRef.current]);

  // Polling (visibility-aware).
  useEffect(() => {
    if (intervalMs <= 0 || !enabled) return;
    let timer: ReturnType<typeof setTimeout> | null = null;

    const loop = () => {
      timer = setTimeout(() => {
        if (typeof document !== "undefined" && document.hidden) {
          // Pause background polling while the tab is hidden.
          loop();
          return;
        }
        run();
        loop();
      }, intervalMs);
    };
    loop();
    return () => {
      if (timer) clearTimeout(timer);
    };
  }, [intervalMs, enabled, run]);

  const refetch = useCallback(() => {
    setTick((t) => t + 1);
  }, []);

  return { data, loading, stale, error, errorDetail, fetchedAt, refetch };
}

/** Polling interval presets (desktop app guidance: analysis 5s, health 10s). */
export const POLL = {
  analysis: 5000,
  health: 10000,
  slow: 30000,
  candles: 15000,
  once: 0,
} as const;
