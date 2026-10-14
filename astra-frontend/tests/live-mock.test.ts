// @vitest-environment node
// Node (undici) fetch + Node AbortSignal: the browser-compatible path the app
// uses. jsdom's AbortSignal is not accepted by undici, so this file opts out of
// jsdom explicitly rather than weakening the client for the test harness.
/**
 * OPT-IN integration test — the real client against the real dev backend.
 *
 * Runs only when ASTRA_LIVE_MOCK=1 and the T19 mock is listening, e.g.
 *
 *   python3 scripts/mock_api.py --port 8790 &
 *   ASTRA_LIVE_MOCK=1 ASTRA_LIVE_BASE=http://127.0.0.1:8790/api/v1/ npx vitest run tests/live-mock.test.ts
 *
 * It proves the frozen route families, the envelope decoding and the
 * nine-timeframe candle series work against a server, not just fixtures.
 */
import { describe, expect, it } from "vitest";
import { configure, friendlyError } from "api/client";
import * as api from "api/endpoints";
import type { Timeframe } from "api/types";
import { TIMEFRAMES } from "api/types";
import { formatScore } from "api/honors";

// Read the two opt-in variables without pulling in Node type definitions: the
// node test environment exposes `process` at runtime, and this file declares
// just the shape it uses.
const nodeEnv =
  (globalThis as { process?: { env?: Record<string, string | undefined> } }).process?.env ?? {};
const LIVE = nodeEnv.ASTRA_LIVE_MOCK === "1";
const BASE = nodeEnv.ASTRA_LIVE_BASE ?? "http://127.0.0.1:8790/api/v1/";

if (LIVE) configure(BASE);

describe.skipIf(!LIVE)("live mock backend (frozen v1 contract)", () => {
  it("decodes every frozen route family", async () => {
    const state = await api.getSystemState();
    expect(state.mode).toBeTruthy();
    expect(typeof state.shadow_only).toBe("boolean");

    const health = await api.getHealth();
    expect(health.aggregate).toBeTruthy();
    expect(Array.isArray(health.degraded_reasons)).toBe(true);

    const v1 = await api.getHealthV1();
    expect(["ok", "degraded", "offline"]).toContain(v1.status);

    const bridge = await api.getBridgeStatus();
    expect(bridge.transport).toBeTruthy();
    expect(bridge.loopback_only).toBe(true);

    const risk = await api.getRiskLatest();
    expect(typeof risk.available).toBe("boolean");

    const gov = await api.getGovernanceStatus();
    expect(gov.live_trading_authorised).toBe(false);

    const analysis = await api.getAnalysisLatest();
    expect(analysis.symbol).toBe("XAUUSD");

    expect(Array.isArray(await api.getAnalysisHistory(5))).toBe(true);
    expect(await api.getContextLatest()).toBeTruthy();
    expect(Array.isArray(await api.getShadowPositions())).toBe(true);
    expect(Array.isArray(await api.getShadowOutcomes())).toBe(true);
    expect(await api.getResearchStatus()).toBeTruthy();
    expect(await api.getAuditRecent()).toBeTruthy();
    expect(Array.isArray(await api.getTimeframes())).toBe(true);
    expect(await api.getTimeframeSnapshot("M15")).toBeTruthy();
  });

  it("serves all nine timeframes in canonical order", async () => {
    const rows = await api.getTimeframes();
    expect(rows.map((r) => r.timeframe)).toEqual([...TIMEFRAMES]);
  });

  it("serves a closed-bar series for every timeframe", async () => {
    for (const tf of TIMEFRAMES) {
      const series = await api.getCandles(tf as Timeframe, 60);
      expect(series.timeframe).toBe(tf);
      expect(series.symbol).toBe("XAUUSD");
      expect(series.bars.length).toBeGreaterThan(0);
      for (const bar of series.bars) {
        expect(Number.isFinite(bar.open)).toBe(true);
        expect(Number.isFinite(bar.high)).toBe(true);
        expect(Number.isFinite(bar.low)).toBe(true);
        expect(Number.isFinite(bar.close)).toBe(true);
        expect(bar.high).toBeGreaterThanOrEqual(bar.low);
        expect(bar.time).toBeGreaterThan(0);
      }
    }
  });

  it("keeps the uncalibrated score honest end-to-end", async () => {
    const analysis = await api.getAnalysisLatest();
    if (!analysis.signal.probability_calibrated) {
      expect(analysis.signal.probability).toBeNull();
      const shown = formatScore(analysis.signal);
      expect(shown.kind).toBe("score");
      expect(shown.label).not.toContain("%");
    }
    expect(analysis.meta.disclaimer.length).toBeGreaterThan(0);
  });

  it("reports the undocumented legacy surfaces as genuinely absent (404)", async () => {
    const err = await api.getLatestSignal().catch((e: unknown) => e);
    expect(friendlyError(err).title).toBe("API ERROR — NOT_FOUND");
  });

  it("documents the read-only mock's rejection of the command write path", async () => {
    // The T19 mock is GET-only, so POST /command answers 405 here. The write
    // path is contract-tested in tests/client.test.ts and tests/command.test.tsx
    // against the documented 202/400/403 bodies; only the real backend can
    // serve it live.
    const err = await api
      .postCommand({ command: "notify", actor: "operator", payload: "hello" })
      .catch((e: unknown) => e);
    expect(err).toBeInstanceOf(Error);
    expect(friendlyError(err).title).toMatch(/API ERROR|BACKEND/);
  });
});
