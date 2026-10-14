/**
 * Centralized API client for the AURA backend (API v1, schema 1.0).
 *
 * Every component goes through this client — components never build their own
 * fetch calls. The client handles:
 *  - configurable base URL (VITE_API_BASE_URL)
 *  - request timeouts + AbortSignal cancellations of obsolete requests
 *  - success envelope decoding {api,schema,data}
 *  - flat error decoding {error,code,message}
 *  - network / disconnected / invalid-response errors
 *  - bounded retry with exponential backoff (idempotent GETs only)
 *
 * Honesty rules from ASTRA_FRONTEND_HANDOFF.md + FRONTEND_HANDOFF_GUIDE.md
 * are implemented in `honors.ts` (score/probability gating) — this file deals
 * only with transport and envelope decoding.
 */

import type { ApiErrorBody, Envelope } from "./types";

/**
 * Determines the API base URL.
 * Priority order:
 *  1. explicit configure() call (tests)
 *  2. VITE_API_BASE_URL env var
 *  3. demo mode ($VITE_DEMO_MODE) → same-origin /api/v1 (mock served locally)
 *  4. none configured → the UI shows "CLOUD CONNECTION NOT CONFIGURED"
 *     rather than silently falling back to localhost.
 */
const rawBase = (import.meta.env?.VITE_API_BASE_URL as string | undefined) ?? "";
export const DEMO_MODE = import.meta.env?.VITE_DEMO_MODE === "1";
const fallbackBase = DEMO_MODE ? "/api/v1/" : "";

let _baseUrl: string | null = rawBase.trim() !== "" ? rawBase.trim() : fallbackBase;

export const AURA_DEFAULT_LOCAL = "http://127.0.0.1:8790/api/v1/";

export function isConfigured(): boolean {
  return _baseUrl !== null && _baseUrl !== "";
}

export function getBaseUrl(): string | null {
  return _baseUrl;
}

export function configure(baseUrl: string | null): void {
  if (baseUrl === null || baseUrl === undefined || baseUrl.trim() === "") {
    _baseUrl = null;
  } else {
    _baseUrl = baseUrl.trim().replace(/\/+$/, "") + "/";
  }
}

export type ConnectionState =
  | "CONNECTED"
  | "UNCONFIGURED"
  | "OFFLINE"
  | "DEGRADED";

/* ---------------------------------------------------------------- */

export class ApiClientError extends Error {
  /** Stable machine code, e.g. dependency_unavailable, not_found... */
  readonly code: string;
  readonly status: number;
  /** True for network-level failures (server down / CORS / DNS). */
  readonly network: boolean;

  constructor(message: string, opts: { code?: string; status?: number; network?: boolean } = {}) {
    super(message);
    this.name = "ApiClientError";
    this.code = opts.code ?? "client_error";
    this.status = opts.status ?? 0;
    this.network = opts.network ?? false;
  }
}

const DEFAULT_TIMEOUT_MS = 8000;
const MAX_RETRIES = 2;

function isEnvelope<T>(body: unknown): body is Envelope<T> {
  if (body === null || typeof body !== "object") return false;
  const b = body as Record<string, unknown>;
  return (
    typeof b.api === "string" &&
    b.api === "v1" &&
    typeof b.schema === "string" &&
    b.schema === "1.0" &&
    "data" in b
  );
}

function isFlatError(body: unknown): body is ApiErrorBody {
  if (body === null || typeof body !== "object") return false;
  const b = body as Record<string, unknown>;
  const err = b.error;
  return (
    (err === true || err === "true") &&
    typeof b.code === "string" &&
    typeof b.message === "string"
  );
}

/** Builds an absolute URL against the configured base. */
export function buildUrl(
  path: string,
  params?: Record<string, string | number | undefined>,
): string {
  const base = _baseUrl ?? "";
  const cleanPath = path.startsWith("/") ? path.slice(1) : path;
  const qs = params
    ? Object.entries(params)
        .filter(([, v]) => v !== undefined)
        .map(
          ([k, v]) =>
            `${encodeURIComponent(k)}=${encodeURIComponent(String(v))}`,
        )
        .join("&")
    : "";
  const url = `${base}${cleanPath}${qs ? `?${qs}` : ""}`;
  return url;
}

interface RequestOptions {
  signal?: AbortSignal;
  timeoutMs?: number;
  retries?: number;
  /**
   * Envelope policy for the response body.
   *  - "required" (default): the frozen success envelope {api,schema,data} is
   *    mandatory; anything else is a protocol error.
   *  - "optional": used for POST /command, whose documented 202 success body is
   *    a *flat* object (`{"accepted":true,"message_id":...}` — ASTRA_FRONTEND_
   *    HANDOFF.md §8) while errors stay flat `{error,code,message}`. A flat
   *    object is accepted as the payload; a flat error still throws.
   */
  envelope?: "required" | "optional";
}

/** Throwable standard error for a response that was neither envelope nor error. */
async function decodeBody<T>(res: Response, envelope: "required" | "optional" = "required"): Promise<T> {
  let body: unknown = null;
  try {
    const text = await res.text();
    if (text.length === 0) {
      throw new ApiClientError(`empty response body (HTTP ${res.status})`, {
        code: "empty_response",
        status: res.status,
      });
    }
    body = JSON.parse(text);
  } catch (err) {
    if (err instanceof ApiClientError) throw err;
    throw new ApiClientError(`unparseable JSON response (HTTP ${res.status})`, {
      code: "invalid_json",
      status: res.status,
    });
  }

  if (isEnvelope<T>(body)) return body.data;

  if (isFlatError(body)) {
    throw new ApiClientError(body.message, {
      code: body.code,
      status: res.status,
    });
  }

  // POST /command success bodies are flat by contract (see RequestOptions).
  if (envelope === "optional" && body !== null && typeof body === "object") {
    return body as T;
  }

  throw new ApiClientError(`unexpected response shape (HTTP ${res.status})`, {
    code: "unexpected_shape",
    status: res.status,
  });
}

function friendlyStatusMessage(status: number, code: string): string {
  switch (status) {
    case 400:
      return code === "actor_required"
        ? "Command rejected: actor attribution is required."
        : `Bad request (${code}).`;
    case 403:
      return "Requested command is not permitted by backend policy.";
    case 404:
      return "Route not found — backend may be older than this frontend.";
    case 405:
      return "Method not allowed for that route.";
    case 413:
      return "Request body too large.";
    case 429:
      return "Rate limited / queue full — slow down and retry.";
    case 502:
      return "Bad gateway — an upstream dependency failed.";
    case 503:
      return "A backend dependency is unavailable — capability offline.";
    default:
      return `HTTP ${status} (${code}).`;
  }
}

/**
 * Single request with timeout + AbortSignal.
 * Throws ApiClientError in all failure cases.
 */
async function requestOnce<T>(
  url: string,
  init: RequestInit,
  opts: Required<Pick<RequestOptions, "timeoutMs">> & {
    signal?: AbortSignal;
    envelope?: "required" | "optional";
  },
): Promise<T> {
  const ctrl = new AbortController();
  // Track which cause aborted the request so a timeout is never reported as a
  // superseded/cancelled poll (and vice-versa).
  let timedOut = false;
  const timer = setTimeout(() => {
    timedOut = true;
    ctrl.abort(new DOMException("timeout", "TimeoutError"));
  }, opts.timeoutMs);
  const onExternalAbort = () => ctrl.abort(new DOMException("aborted", "AbortError"));
  opts.signal?.addEventListener("abort", onExternalAbort, { once: true });

  try {
    let res: Response;
    try {
      res = await fetch(url, {
        ...init,
        signal: ctrl.signal,
        headers: { Accept: "application/json", ...(init.headers ?? {}) },
      });
    } catch (err) {
      // Caller-initiated cancellation is not an error worth reporting.
      if (opts.signal?.aborted) throw new DOMException("aborted", "AbortError");
      // Distinguish a timeout from a plain unreachable backend.
      if (timedOut) {
        throw new ApiClientError(`request timed out after ${opts.timeoutMs}ms`, {
          code: "timeout",
          status: 0,
          network: true,
        });
      }
      if (ctrl.signal.aborted) throw new DOMException("aborted", "AbortError");
      // Network failure: server down / CORS / DNS.
      void err;
      throw new ApiClientError("backend unreachable (network error)", {
        code: "network_error",
        status: 0,
        network: true,
      });
    }

    const data = await decodeBody<T>(res, opts.envelope ?? "required");
    return data;
  } finally {
    clearTimeout(timer);
    opts.signal?.removeEventListener("abort", onExternalAbort);
  }
}

/** GET with bounded exponential-backoff retry (never retries on 4xx). */
export async function getJson<T>(
  path: string,
  params?: Record<string, string | number | undefined>,
  opts: RequestOptions = {},
): Promise<T> {
  if (!isConfigured()) {
    throw new ApiClientError("API base URL not configured", { code: "not_configured" });
  }
  const url = buildUrl(path, params);
  const timeoutMs = opts.timeoutMs ?? DEFAULT_TIMEOUT_MS;
  const maxRetries = opts.retries ?? MAX_RETRIES;

  let lastErr: unknown = null;
  for (let attempt = 0; attempt <= maxRetries; attempt++) {
    try {
      return await requestOnce<T>(url, { method: "GET" }, { timeoutMs, signal: opts.signal });
    } catch (err) {
      if (err instanceof DOMException && err.name === "AbortError" && opts.signal?.aborted) {
        throw err; // caller cancels: no retry
      }
      lastErr = err;
      const retryable =
        (err instanceof ApiClientError && (err.network || err.status >= 500)) ||
        !(err instanceof ApiClientError);
      if (!retryable || attempt === maxRetries) break;
      // exponential backoff 400ms, 800ms
      await new Promise((r) => setTimeout(r, 400 * 2 ** attempt));
    }
  }
  if (lastErr instanceof ApiClientError) {
    throw lastErr;
  }
  if (lastErr instanceof DOMException && lastErr.name === "AbortError") {
    throw lastErr;
  }
  throw new ApiClientError(lastErr instanceof Error ? lastErr.message : String(lastErr), { code: "unknown" });
}

/** POST — never retried automatically (writes must not be sent twice). */
export async function postJson<TBody extends object, TRes>(
  path: string,
  body: TBody,
  opts: RequestOptions = {},
): Promise<TRes> {
  if (!isConfigured()) {
    throw new ApiClientError("API base URL not configured", { code: "not_configured" });
  }
  const url = buildUrl(path);
  return requestOnce<TRes>(
    url,
    {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    },
    {
      timeoutMs: opts.timeoutMs ?? DEFAULT_TIMEOUT_MS,
      signal: opts.signal,
      // The documented 202 success body for /command is flat, not enveloped.
      envelope: opts.envelope ?? "optional",
    },
  );
}

export function friendlyError(err: unknown): { title: string; detail: string } {
  if (err instanceof ApiClientError) {
    if (err.code === "timeout") {
      return {
        title: "BACKEND TIMEOUT",
        detail: `${err.message}. The API is reachable but did not answer in time — it may be busy with MT5 or the bridge is stalled.`,
      };
    }
    if (err.network) {
      return {
        title: "BACKEND OFFLINE",
        detail: "Could not reach the AURA backend API. It may not be running, or the configured URL is wrong.",
      };
    }
    if (err.code === "not_configured") {
      return {
        title: "CLOUD CONNECTION NOT CONFIGURED",
        detail: "No API base URL is set. Configure VITE_API_BASE_URL (see .env.example).",
      };
    }
    return {
      title: `API ERROR — ${err.code.toUpperCase()}`,
      detail: friendlyStatusMessage(err.status, err.code) + (err.message ? ` ${err.message}` : ""),
    };
  }
  if (err instanceof DOMException && err.name === "AbortError") {
    return { title: "REQUEST CANCELLED", detail: "Polling request was superseded." };
  }
  return {
    title: "UNEXPECTED ERROR",
    detail: err instanceof Error ? err.message : String(err),
  };
}

// End of client.
