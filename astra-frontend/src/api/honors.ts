/**
 * Honesty rules — the single place where score / probability /
 * data-quality display semantics are decided. Used by every screen.
 *
 * From FRONTEND_HANDOFF_GUIDE.md §D and the T17 freeze:
 *  - `signal.probability` is non-null ONLY when calibrated AND audited.
 *  - Use `signal.probability_calibrated` (NOT `meta.score_is_probability`)
 *    to decide probability-vs-score display.
 *  - The score is a raw, opaque number with unresolved scale: never assume
 *    0–1 or 0–100, never convert it into a probability, never show a
 *    percent symbol for it.
 *  - Null/UNKNOWN fields render "—"/N/A; never zero, never "safe".
 */

import type {
  AnalysisSignal,
  CoverageTier,
  FreshnessState,
  TimeframeRow,
} from "./types";

export interface ScoreDisplay {
  /** "score" ⇒ raw score styling, "prob" ⇒ probability. */
  kind: "score" | "prob" | "none";
  /** Pre-rendered string; already carries the right semantics. */
  label: string;
  detail: string;
}

export const UNAVAILABLE = "—";

export function formatScore(s: AnalysisSignal): ScoreDisplay {
  if (s.probability_calibrated && s.probability !== null && s.probability !== undefined) {
    // Calibrated AND published: it is a real probability in [0,1].
    return {
      kind: "prob",
      label: `${(s.probability * 100).toFixed(1)}%`,
      detail: "Calibrated probability P(UP) published by the backend.",
    };
  }
  if (s.score === null || s.score === undefined) {
    return { kind: "none", label: UNAVAILABLE, detail: "Score not available." };
  }
  // Raw opaque score: display verbatim, no scale assumption, never a percent.
  return {
    kind: "score",
    label: String(s.score),
    detail: "Raw score — not a probability; scale is backend-defined.",
  };
}

/**
 * Label used wherever the number is presented on screen. Accepts the minimal
 * calibration fields so table headers can be labelled before the first row
 * exists (an absent signal is a SCORE header, never PROBABILITY).
 */
export function scoreFieldLabel(
  s: Pick<AnalysisSignal, "probability_calibrated" | "probability">,
): string {
  return s.probability_calibrated && s.probability != null ? "PROBABILITY" : "SCORE";
}

/** Probability text for detailed panels, honouring RULE C. */
export function probabilityOrNullText(s: AnalysisSignal): string {
  if (s.probability_calibrated && s.probability !== null) {
    return (s.probability * 100).toFixed(1) + "%";
  }
  return "N/A — CALIBRATION NOT AUTHORISED";
}

export function coverageLabel(t: CoverageTier | undefined | null): string {
  if (!t) return "UNKNOWN";
  return t.toUpperCase();
}

/** Data-quality / freshness badge tone for a row of the timeframe matrix. */
export type Tone = "up" | "down" | "warn" | "neutral" | "accent" | "critical";

export function qualityTone(row: Pick<TimeframeRow, "observed" | "quality" | "freshness">): Tone {
  if (!row.observed) return "neutral";
  const state = (row.quality?.state ?? "UNKNOWN").toUpperCase();
  if (state === "VALID") return "up";
  if (state === "STALE") return "warn";
  if (state === "DEGRADED" || state === "INCOMPLETE" || state === "OUT_OF_ORDER" || state === "DUPLICATE") return "warn";
  if (state === "INVALID" || state === "MISSING") return "critical";
  return "neutral"; // UNKNOWN and anything unrecognized
}

export function freshnessTone(f: FreshnessState | undefined | null): Tone {
  switch (f) {
    case "FRESH":
      return "up";
    case "STALE":
      return "warn";
    default:
      return "neutral"; // UNKNOWN/null
  }
}

/** System/service states → chip tone. Notably: unknown is never green. */
export function serviceStateTone(state: string | null | undefined): Tone {
  const s = (state ?? "UNKNOWN").toUpperCase();
  switch (s) {
    case "ONLINE":
    case "NORMAL":
    case "OK":
    case "VALID":
      return "up";
    case "SHADOW":
      return "accent";
    case "STARTING":
    case "RECOVERY":
    case "RECOVERING":
      return "warn";
    case "DEGRADED":
    case "PAUSED":
      return "warn";
    case "BLOCKED":
      return "critical";
    case "EMERGENCY":
    case "HALTED":
    case "ERROR":
      return "critical";
    case "MANUAL":
      return "neutral";
    case "OFFLINE":
      return "down";
    default:
      return "neutral"; // UNKNOWN & unrecognized values
  }
}

export function healthStatusTone(status: string | null | undefined): Tone {
  switch ((status ?? "").toUpperCase()) {
    case "OK":
      return "up";
    case "DEGRADED":
      return "warn";
    case "OFFLINE":
      return "down";
    default:
      return "neutral";
  }
}

export function directionTone(dir: string | null | undefined): Tone {
  switch (dir) {
    case "UP":
    case "LONG":
      return "up";
    case "DOWN":
    case "SHORT":
      return "down";
    default:
      return "neutral"; // NONE, FLAT, UNKNOWN, null
  }
}

export function formatDirection(dir: string | null | undefined): string {
  if (!dir || dir === "NONE" || dir === "UNKNOWN" || dir === "FLAT") {
    return "NONE";
  }
  return dir;
}

/** epoch millis or epoch secs → "2026-10-07 22:03:11 UTC". Returns "—" when null. */
export function formatUtcTime(epoch: number | string | null | undefined): string {
  if (epoch === null || epoch === undefined) return UNAVAILABLE;
  let ms: number;
  if (typeof epoch === "string") {
    if (epoch.length === 0) return UNAVAILABLE;
    const parsed = Date.parse(epoch);
    if (Number.isNaN(parsed)) return UNAVAILABLE;
    ms = parsed;
  } else {
    ms = epoch > 1e12 ? epoch : epoch * 1000; // heuristic: sec vs millis
  }
  const d = new Date(ms);
  if (Number.isNaN(d.getTime())) return UNAVAILABLE;
  const p = (n: number) => String(n).padStart(2, "0");
  return `${d.getUTCFullYear()}-${p(d.getUTCMonth() + 1)}-${p(d.getUTCDate())} ${p(
    d.getUTCHours(),
  )}:${p(d.getUTCMinutes())}:${p(d.getUTCSeconds())} UTC`;
}

/** epoch → "2026-10-07 22:03 UTC" (chart axes, compact) */
export function formatUtcShort(epochSec: number): string {
  const d = new Date(epochSec * 1000);
  if (Number.isNaN(d.getTime())) return String(epochSec);
  const p = (n: number) => String(n).padStart(2, "0");
  const time = `${p(d.getUTCHours())}:${p(d.getUTCMinutes())}`;
  if (d.getUTCHours() === 0 && d.getUTCMinutes() === 0) {
    return `${p(d.getUTCDate())}/${p(d.getUTCMonth() + 1)}`;
  }
  return time;
}

/** "just now" / "12s ago" / "3m ago" / "2h ago" / older as absolute UTC. */
export function ageText(fetchedAtMs: number | null, nowMs: number = Date.now()): string {
  if (fetchedAtMs === null) return "no data yet";
  const s = Math.max(0, Math.round((nowMs - fetchedAtMs) / 1000));
  if (s < 3) return `updated ${s}s ago`;
  if (s < 60) return `updated ${s}s ago`;
  const m = Math.floor(s / 60);
  if (m < 60) return `updated ${m}m ago`;
  const h = Math.floor(m / 60);
  if (h < 24) return `updated ${h}h ago`;
  return `updated ${Math.floor(h / 24)}d ago`;
}

/** Number formatting for prices (XAUUSD ~ 2 decimals). */
export function fmtPrice(v: number | null | undefined): string {
  if (v === null || v === undefined || Number.isNaN(v)) return UNAVAILABLE;
  return v.toFixed(2);
}

export function fmtNumber(v: number | null | undefined, digits = 2): string {
  if (v === null || v === undefined || Number.isNaN(v)) return UNAVAILABLE;
  return v.toFixed(digits);
}

/** data freshness label from meta.data_freshness_sec (null = unknown). */
export function freshnessLabel(sec: number | null | undefined): { label: string; tone: Tone } {
  if (sec === null || sec === undefined) return { label: "UNKNOWN", tone: "neutral" };
  if (sec <= 15) return { label: "FRESH", tone: "up" };
  if (sec <= 60) return { label: "RECENT", tone: "accent" };
  if (sec <= 300) return { label: "AGING", tone: "warn" };
  return { label: "STALE", tone: "down" };
}
