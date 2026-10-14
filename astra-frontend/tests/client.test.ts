/**
 * API client transport tests — URL composition, envelope decoding, flat error
 * bodies, invalid responses, disconnection, timeout, retry/backoff and the
 * POST /command flat-success contract.
 */
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import {
  ApiClientError,
  buildUrl,
  configure,
  friendlyError,
  getBaseUrl,
  getJson,
  isConfigured,
  postJson,
} from "api/client";
import { fixtureBody, fixtureData } from "./helpers/fixtures";
import { jsonResponse, textResponse } from "./helpers/mockApi";

const BASE = "http://127.0.0.1:8790/api/v1/";

beforeEach(() => {
  configure(BASE);
});

afterEach(() => {
  configure(null);
  vi.useRealTimers();
});

describe("base URL composition", () => {
  it("reports configured/unconfigured state honestly", () => {
    expect(isConfigured()).toBe(true);
    expect(getBaseUrl()).toBe(BASE);
    configure(null);
    expect(isConfigured()).toBe(false);
    expect(getBaseUrl()).toBeNull();
  });

  it("normalises the trailing slash", () => {
    configure("http://127.0.0.1:8790/api/v1");
    expect(getBaseUrl()).toBe(BASE);
    configure("http://127.0.0.1:8790/api/v1///");
    expect(getBaseUrl()).toBe(BASE);
  });

  it("joins paths and encodes query parameters", () => {
    expect(buildUrl("candles", { tf: "M15", limit: 500 })).toBe(
      `${BASE}candles?tf=M15&limit=500`,
    );
    expect(buildUrl("/timeframes/M15/snapshot")).toBe(`${BASE}timeframes/M15/snapshot`);
  });

  it("drops undefined parameters instead of sending 'undefined'", () => {
    expect(buildUrl("candles", { tf: "H4", limit: undefined })).toBe(`${BASE}candles?tf=H4`);
  });

  it("refuses to fetch when no base URL is configured", async () => {
    configure(null);
    const fetchSpy = vi.fn();
    globalThis.fetch = fetchSpy as unknown as typeof fetch;
    await expect(getJson("health")).rejects.toMatchObject({ code: "not_configured" });
    expect(fetchSpy).not.toHaveBeenCalled();
  });
});

describe("success envelope decoding", () => {
  it("unwraps {api,schema,data}", async () => {
    globalThis.fetch = vi.fn(async () =>
      jsonResponse(200, fixtureBody("valid", "health")),
    ) as unknown as typeof fetch;

    const data = await getJson<Record<string, unknown>>("health");
    expect(data).toEqual(fixtureData("health"));
  });

  it("rejects a bare (un-enveloped) object", async () => {
    globalThis.fetch = vi.fn(async () =>
      jsonResponse(200, fixtureBody("invalid", "not_enveloped")),
    ) as unknown as typeof fetch;

    await expect(getJson("analysis/latest")).rejects.toMatchObject({
      code: "unexpected_shape",
    });
  });

  it("rejects a wrong envelope api constant", async () => {
    globalThis.fetch = vi.fn(async () =>
      jsonResponse(200, fixtureBody("invalid", "wrong_envelope_api_const")),
    ) as unknown as typeof fetch;

    await expect(getJson("analysis/latest")).rejects.toMatchObject({
      code: "unexpected_shape",
    });
  });

  it("rejects an envelope missing the schema key", async () => {
    globalThis.fetch = vi.fn(async () =>
      jsonResponse(200, fixtureBody("invalid", "missing_envelope_schema_key")),
    ) as unknown as typeof fetch;

    await expect(getJson("analysis/latest")).rejects.toMatchObject({
      code: "unexpected_shape",
    });
  });

  it("surfaces an empty body and unparseable JSON distinctly", async () => {
    globalThis.fetch = vi.fn(async () => textResponse(200, "")) as unknown as typeof fetch;
    await expect(getJson("health")).rejects.toMatchObject({ code: "empty_response" });

    globalThis.fetch = vi.fn(async () => textResponse(200, "<html>oops")) as unknown as typeof fetch;
    await expect(getJson("health")).rejects.toMatchObject({ code: "invalid_json" });
  });
});

describe("flat error bodies", () => {
  it("maps 404 to the backend code and a useful message", async () => {
    globalThis.fetch = vi.fn(async () =>
      jsonResponse(404, fixtureBody("errors", "404_unknown_route")),
    ) as unknown as typeof fetch;

    const err = await getJson("nope").catch((e: unknown) => e);
    expect(err).toBeInstanceOf(ApiClientError);
    const fe = friendlyError(err);
    expect(fe.title).toBe("API ERROR — NOT_FOUND");
    expect(fe.detail).toContain("Route not found");
    // 404s are not retried.
    expect((globalThis.fetch as unknown as { mock: { calls: unknown[] } }).mock.calls).toHaveLength(1);
  });

  it("maps 405 and 503 with status-specific guidance", async () => {
    globalThis.fetch = vi.fn(async () =>
      jsonResponse(405, fixtureBody("errors", "405_method_not_allowed")),
    ) as unknown as typeof fetch;
    const e405 = await getJson("candles").catch((e: unknown) => e);
    expect(friendlyError(e405).detail).toContain("Method not allowed");

    globalThis.fetch = vi.fn(async () =>
      jsonResponse(503, fixtureBody("errors", "503_missing_dependency")),
    ) as unknown as typeof fetch;
    const e503 = await getJson("bridge/status", undefined, { retries: 0 }).catch((e: unknown) => e);
    expect(friendlyError(e503).detail).toContain("dependency is unavailable");
  });
});

describe("disconnection, timeout and retry", () => {
  it("reports a network failure as BACKEND OFFLINE", async () => {
    globalThis.fetch = vi.fn(async () => {
      throw new TypeError("Failed to fetch");
    }) as unknown as typeof fetch;

    const err = await getJson("health", undefined, { retries: 0 }).catch((e: unknown) => e);
    expect(err).toMatchObject({ code: "network_error", network: true });
    expect(friendlyError(err).title).toBe("BACKEND OFFLINE");
  });

  it("reports a timeout as BACKEND TIMEOUT, not as a cancelled poll", async () => {
    globalThis.fetch = vi.fn(
      (_url: unknown, init?: RequestInit) =>
        new Promise((_resolve, reject) => {
          init?.signal?.addEventListener("abort", () =>
            reject(new DOMException("aborted", "AbortError")),
          );
        }),
    ) as unknown as typeof fetch;

    const err = await getJson("health", undefined, { timeoutMs: 20, retries: 0 }).catch(
      (e: unknown) => e,
    );
    expect(err).toMatchObject({ code: "timeout" });
    expect(friendlyError(err).title).toBe("BACKEND TIMEOUT");
  });

  it("does not spin on caller cancellation", async () => {
    const ctrl = new AbortController();
    globalThis.fetch = vi.fn(
      (_url: unknown, init?: RequestInit) =>
        new Promise((_resolve, reject) => {
          init?.signal?.addEventListener("abort", () =>
            reject(new DOMException("aborted", "AbortError")),
          );
        }),
    ) as unknown as typeof fetch;

    const p = getJson("health", undefined, { signal: ctrl.signal, retries: 2 });
    ctrl.abort();
    await expect(p).rejects.toMatchObject({ name: "AbortError" });
    expect((globalThis.fetch as unknown as { mock: { calls: unknown[] } }).mock.calls).toHaveLength(1);
  });

  it("retries only retryable failures (5xx / network) with bounded backoff", async () => {
    let attempt = 0;
    globalThis.fetch = vi.fn(async () => {
      attempt += 1;
      if (attempt === 1) return jsonResponse(503, fixtureBody("errors", "503_missing_dependency"));
      return jsonResponse(200, fixtureBody("valid", "bridge_status"));
    }) as unknown as typeof fetch;

    const data = await getJson<Record<string, unknown>>("bridge/status", undefined, { retries: 1 });
    expect(attempt).toBe(2);
    expect(data).toEqual(fixtureData("bridge_status"));
  });
});

describe("POST /command (the only write surface)", () => {
  it("accepts the documented flat 202 success body", async () => {
    const fetchSpy = vi.fn(async (_url: unknown, _init?: RequestInit) =>
      jsonResponse(202, { accepted: true, request_id: "approval-1", status: "PENDING" }),
    );
    globalThis.fetch = fetchSpy as unknown as typeof fetch;

    const res = await postJson("command", {
      command: "request_approval",
      actor: "operator",
      payload: "raise risk cap",
    });
    expect(res).toEqual({ accepted: true, request_id: "approval-1", status: "PENDING" });

    const init = fetchSpy.mock.calls[0]?.[1];
    expect(init?.method).toBe("POST");
    expect(init?.headers).toMatchObject({ "Content-Type": "application/json" });
    expect(JSON.parse(String(init?.body))).toEqual({
      command: "request_approval",
      actor: "operator",
      payload: "raise risk cap",
    });
  });

  it("surfaces a rejected command (403) rather than a fake success", async () => {
    globalThis.fetch = vi.fn(async () =>
      jsonResponse(403, { error: true, code: "command_not_permitted", message: "not permitted" }),
    ) as unknown as typeof fetch;

    await expect(
      postJson("command", { command: "notify", actor: "op", payload: "x" }),
    ).rejects.toMatchObject({ code: "command_not_permitted", status: 403 });
  });

  it("never retries a write", async () => {
    const fetchSpy = vi.fn(async () => {
      throw new TypeError("Failed to fetch");
    });
    globalThis.fetch = fetchSpy as unknown as typeof fetch;

    await expect(
      postJson("command", { command: "notify", actor: "op", payload: "x" }),
    ).rejects.toBeInstanceOf(ApiClientError);
    expect(fetchSpy).toHaveBeenCalledTimes(1);
  });

  it("still rejects an unknown flat error body when envelopes are optional", async () => {
    globalThis.fetch = vi.fn(async () =>
      jsonResponse(400, { error: true, code: "actor_required", message: "actor required" }),
    ) as unknown as typeof fetch;

    await expect(
      postJson("command", { command: "notify", actor: "", payload: "" }),
    ).rejects.toMatchObject({ code: "actor_required" });
  });
});
