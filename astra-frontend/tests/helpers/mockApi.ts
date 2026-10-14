/**
 * Minimal fetch double. It answers with the frozen contract fixtures so that
 * screens can be exercised end-to-end (hook → endpoint → client → decode)
 * without a backend, without inventing response bodies and without jsdom's
 * Response implementation.
 */
import { vi } from "vitest";
import { ROUTE_FIXTURES, fixtureBody } from "./fixtures";

export interface MockResponseLike {
  status: number;
  ok: boolean;
  text: () => Promise<string>;
}

export function jsonResponse(status: number, body: unknown): MockResponseLike {
  return {
    status,
    ok: status >= 200 && status < 300,
    text: async () => JSON.stringify(body),
  };
}

export function textResponse(status: number, body: string): MockResponseLike {
  return { status, ok: status >= 200 && status < 300, text: async () => body };
}

export interface MockApi {
  calls: Array<{ url: string; method: string; body: string | null }>;
  urls: () => string[];
}

/**
 * Installs a global fetch that serves the frozen fixtures.
 * Pass `override` to intercept a specific URL fragment (e.g. force a 503).
 */
export function installMockApi(
  override?: (
    url: string,
    init: RequestInit | undefined,
  ) => MockResponseLike | undefined,
): MockApi {
  const calls: MockApi["calls"] = [];

  const fetchMock = vi.fn(async (url: string | URL | Request, init?: RequestInit) => {
    const u = String(url);
    const method = (init?.method ?? "GET").toUpperCase();
    calls.push({ url: u, method, body: (init?.body as string | undefined) ?? null });

    const forced = override?.(u, init);
    if (forced) return forced;

    if (method === "POST") {
      // POST /command → flat 202 body, per ASTRA_FRONTEND_HANDOFF.md §8.
      return jsonResponse(202, { accepted: true, request_id: "approval-1", status: "PENDING" });
    }

    const route = ROUTE_FIXTURES.find((r) => r.match.test(u));
    if (!route) {
      return jsonResponse(404, { error: true, code: "not_found", message: "unknown route" });
    }
    return jsonResponse(200, fixtureBody("valid", route.name));
  });

  globalThis.fetch = fetchMock as unknown as typeof fetch;

  return {
    calls,
    urls: () => calls.map((c) => c.url),
  };
}
