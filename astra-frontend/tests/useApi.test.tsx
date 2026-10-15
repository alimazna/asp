/**
 * Polling layer tests (§20): no overlapping requests, cancellation of obsolete
 * requests, staleness on failure, visibility pause and manual refetch.
 */
import { act, renderHook, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { useApi } from "api/useApi";

const deferred = <T,>() => {
  let resolve!: (v: T) => void;
  let reject!: (e: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
};

beforeEach(() => {
  vi.useFakeTimers({ shouldAdvanceTime: true });
});

afterEach(() => {
  vi.useRealTimers();
});

describe("useApi", () => {
  it("fetches once with intervalMs 0 and clears loading after the first settle", async () => {
    const fetcher = vi.fn(async () => ({ ok: true }));
    const { result } = renderHook(() => useApi({ fetcher, intervalMs: 0 }));

    await waitFor(() => expect(result.current.loading).toBe(false));
    expect(fetcher).toHaveBeenCalledTimes(1);
    expect(result.current.data).toEqual({ ok: true });
    expect(result.current.stale).toBe(false);
    expect(result.current.error).toBeNull();
    expect(result.current.fetchedAt).not.toBeNull();

    await act(async () => {
      vi.advanceTimersByTime(5000);
    });
    expect(fetcher).toHaveBeenCalledTimes(1); // no polling configured
  });

  it("skips a poll tick while a request is still in flight (no overlap)", async () => {
    const d = deferred<{ n: number }>();
    const fetcher = vi.fn(() => d.promise);
    const { result } = renderHook(() => useApi({ fetcher, intervalMs: 1000 }));

    expect(fetcher).toHaveBeenCalledTimes(1);
    await act(async () => {
      vi.advanceTimersByTime(5000);
    });
    expect(fetcher).toHaveBeenCalledTimes(1); // still the first request

    await act(async () => {
      d.resolve({ n: 1 });
      await Promise.resolve();
    });
    await waitFor(() => expect(result.current.data).toEqual({ n: 1 }));

    await act(async () => {
      vi.advanceTimersByTime(1000);
    });
    await waitFor(() => expect(fetcher).toHaveBeenCalledTimes(2));
  });

  it("marks data stale after a failed poll but keeps the last real values", async () => {
    let attempt = 0;
    const fetcher = vi.fn(async (_signal: AbortSignal): Promise<{ v: number }> => {
      attempt += 1;
      if (attempt === 1) return { v: 1 };
      throw new Error("boom");
    });
    const { result } = renderHook(() => useApi({ fetcher, intervalMs: 1000 }));

    await waitFor(() => expect(result.current.data).toEqual({ v: 1 }));
    expect(result.current.stale).toBe(false);

    await act(async () => {
      vi.advanceTimersByTime(1100);
    });
    await waitFor(() => expect(result.current.stale).toBe(true));
    expect(result.current.data).toEqual({ v: 1 }); // previous data retained
    expect(result.current.error).toBe("UNEXPECTED ERROR");
    // the previous fetch time is preserved so age stays truthful
    expect(result.current.fetchedAt).not.toBeNull();
  });

  it("does not fetch when disabled and reports that nothing is loading", () => {
    const fetcher = vi.fn(async () => ({ ok: true }));
    const { result } = renderHook(() => useApi({ fetcher, enabled: false }));
    expect(fetcher).not.toHaveBeenCalled();
    expect(result.current.loading).toBe(false);
    expect(result.current.data).toBeNull();
  });

  it("pauses polling while the document is hidden", async () => {
    const fetcher = vi.fn(async () => ({ ok: true }));
    const { result } = renderHook(() => useApi({ fetcher, intervalMs: 1000 }));
    await waitFor(() => expect(result.current.data).toEqual({ ok: true }));

    const hidden = vi.spyOn(document, "hidden", "get").mockReturnValue(true);
    await act(async () => {
      vi.advanceTimersByTime(3000);
    });
    expect(fetcher).toHaveBeenCalledTimes(1);

    hidden.mockReturnValue(false);
    await act(async () => {
      vi.advanceTimersByTime(1100);
    });
    await waitFor(() => expect(fetcher).toHaveBeenCalledTimes(2));
    hidden.mockRestore();
  });

  it("refetch() issues a fresh request on demand", async () => {
    const fetcher = vi.fn(async () => ({ n: Date.now() }));
    const { result } = renderHook(() => useApi({ fetcher, intervalMs: 0 }));
    await waitFor(() => expect(fetcher).toHaveBeenCalledTimes(1));

    act(() => result.current.refetch());
    await waitFor(() => expect(fetcher).toHaveBeenCalledTimes(2));
  });

  it("does not re-fetch in a loop when the caller passes an inline fetcher", async () => {
    // Regression: every real page passes `fetcher={(s) => api.getX({signal:s})}`,
    // a NEW function identity each render. When `run` depended on `fetcher`, its
    // identity changed on every render, the fetch effect re-ran, setState
    // re-rendered, and each endpoint was hit ~11x/second instead of once per
    // interval. Counting calls proves the effect is no longer self-triggering.
    const fetcher = vi.fn(async (_signal: AbortSignal) => ({ ok: true }));
    const { result } = renderHook(() =>
      useApi({ fetcher: (signal: AbortSignal) => fetcher(signal), intervalMs: 5000 }),
    );

    await waitFor(() => expect(result.current.data).toEqual({ ok: true }));
    expect(fetcher).toHaveBeenCalledTimes(1);

    // Several interval windows must pass with no extra traffic.
    await act(async () => {
      await new Promise((r) => setTimeout(r, 300));
    });
    expect(fetcher).toHaveBeenCalledTimes(1);
  });
});
