/**
 * Typed endpoint wrappers — one function per verified route family
 * (frozen v1 contract). Components use these; they never call fetch
 * directly, and never invent routes.
 */

import { getJson, postJson } from "./client";
import type {
  AnalysisRecord,
  ApiErrorBody,
  AuditRecent,
  CandleSeries,
  CommandAccepted,
  CommandRequest,
  ContextLatest,
  Health,
  HealthV1,
  GovernanceStatus,
  LatestSignal,
  ResearchStatus,
  RiskLatest,
  ShadowOutcome,
  ShadowPosition,
  SystemState,
  Timeframe,
  TimeframeRow,
  TimeframeSnapshot,
} from "./types";

export const getSystemState = (opts?: { signal?: AbortSignal }) =>
  getJson<SystemState>("system/state", undefined, opts);

export const getHealth = (opts?: { signal?: AbortSignal }) =>
  getJson<Health>("health", undefined, opts);

export const getHealthV1 = (opts?: { signal?: AbortSignal }) =>
  getJson<HealthV1>("health/v1", undefined, opts);

export const getTimeframes = (opts?: { signal?: AbortSignal }) =>
  getJson<TimeframeRow[]>("timeframes", undefined, opts);

export const getTimeframeSnapshot = (tf: Timeframe, opts?: { signal?: AbortSignal }) =>
  getJson<TimeframeSnapshot>(`timeframes/${encodeURIComponent(tf)}/snapshot`, undefined, opts);

export const getCandles = (
  tf: Timeframe,
  limit: number,
  opts?: { signal?: AbortSignal },
) => getJson<CandleSeries>("candles", { tf, limit }, opts);

export const getAnalysisLatest = (opts?: { signal?: AbortSignal }) =>
  getJson<AnalysisRecord>("analysis/latest", undefined, opts);

export const getAnalysisHistory = (
  limit: number,
  opts?: { signal?: AbortSignal },
) => getJson<AnalysisRecord[]>("analysis/history", { limit }, opts);

export const getContextLatest = (opts?: { signal?: AbortSignal }) =>
  getJson<ContextLatest>("context/latest", undefined, opts);

/** Legacy signal surface (§7.5 handoff). May 404 on newer builds — treated as optional. */
export const getLatestSignal = (opts?: { signal?: AbortSignal }) =>
  getJson<LatestSignal>("signals/latest", undefined, opts);

/** Legacy probability surface — hidden by §17 honesty rules unless calibrated. */
export const getProbabilityLatest = (opts?: { signal?: AbortSignal }) =>
  getJson<{ probability: number | null; probability_calibrated: boolean }>(
    "probability/latest",
    undefined,
    opts,
  );

export const getRiskLatest = (opts?: { signal?: AbortSignal }) =>
  getJson<RiskLatest>("risk/latest", undefined, opts);

export const getShadowPositions = (opts?: { signal?: AbortSignal }) =>
  getJson<ShadowPosition[]>("shadow/positions", undefined, opts);

export const getShadowOutcomes = (opts?: { signal?: AbortSignal }) =>
  getJson<ShadowOutcome[]>("shadow/outcomes", undefined, opts);

export const getResearchStatus = (opts?: { signal?: AbortSignal }) =>
  getJson<ResearchStatus>("research/status", undefined, opts);

export const getGovernanceStatus = (opts?: { signal?: AbortSignal }) =>
  getJson<GovernanceStatus>("governance/status", undefined, opts);

export const getAuditRecent = (opts?: { signal?: AbortSignal }) =>
  getJson<AuditRecent>("audit/recent", undefined, opts);

export const getBridgeStatus = (opts?: { signal?: AbortSignal }) =>
  getJson<import("./types").BridgeStatus>("bridge/status", undefined, opts);

export const postCommand = (
  body: CommandRequest,
  opts?: { signal?: AbortSignal },
) =>
  postJson<CommandRequest, CommandAccepted>(
    "command",
    body,
    opts,
  );

/** Exposed for tests. */
export type { ApiErrorBody };
