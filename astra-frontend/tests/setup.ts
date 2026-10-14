/**
 * Vitest global setup (referenced from vite.config.ts → test.setupFiles).
 *
 * Adds the jest-dom matchers, unmounts React trees between tests and installs
 * the two browser APIs jsdom does not implement that this app touches:
 * ResizeObserver (the candle canvas) and matchMedia (responsive checks).
 *
 * Everything DOM-related is guarded so that files running in the `node`
 * environment (tests/live-mock.test.ts, which talks to a real dev server) can
 * reuse the same setup file.
 */
import "@testing-library/jest-dom/vitest";
import { afterEach, vi } from "vitest";

afterEach(async () => {
  if (typeof document !== "undefined") {
    const { cleanup } = await import("@testing-library/react");
    cleanup();
  }
  vi.unstubAllGlobals();
  // Only the fixture fetch double is relevant in jsdom; never leak it.
  if (typeof document !== "undefined") {
    delete (globalThis as { fetch?: unknown }).fetch;
  }
});

if (typeof globalThis.ResizeObserver === "undefined") {
  class ResizeObserverStub {
    observe(): void {}
    unobserve(): void {}
    disconnect(): void {}
  }
  globalThis.ResizeObserver = ResizeObserverStub as unknown as typeof ResizeObserver;
}

// jsdom ships no canvas backend: the chart must degrade to "no 2d context"
// instead of throwing (it already guards `getContext(...) === null`).
if (typeof HTMLCanvasElement !== "undefined") {
  HTMLCanvasElement.prototype.getContext = (() =>
    null) as unknown as typeof HTMLCanvasElement.prototype.getContext;
}

if (typeof window !== "undefined" && typeof window.matchMedia !== "function") {
  window.matchMedia = ((query: string) => ({
    matches: false,
    media: query,
    onchange: null,
    addListener: () => {},
    removeListener: () => {},
    addEventListener: () => {},
    removeEventListener: () => {},
    dispatchEvent: () => false,
  })) as unknown as typeof window.matchMedia;
}
