/**
 * Honesty rules (§17 of the build mission, FRONTEND_HANDOFF_GUIDE §D):
 *  - a raw score is never a probability and never gets a "%";
 *  - a probability is shown only when probability_calibrated is true;
 *  - unknown/null never renders as zero, healthy or safe.
 */
import { describe, expect, it } from "vitest";
import type { AnalysisSignal, TimeframeRow } from "api/types";
import {
  UNAVAILABLE,
  ageText,
  coverageLabel,
  directionTone,
  fmtNumber,
  fmtPrice,
  formatDirection,
  formatScore,
  formatUtcShort,
  formatUtcTime,
  freshnessLabel,
  freshnessTone,
  healthStatusTone,
  probabilityOrNullText,
  qualityTone,
  scoreFieldLabel,
  serviceStateTone,
} from "api/honors";
import { fixtureBody, fixtureData } from "./helpers/fixtures";

const signal = (over: Partial<AnalysisSignal> = {}): AnalysisSignal => ({
  direction: "NONE",
  horizon: null,
  probability: null,
  probability_calibrated: false,
  score: null,
  confidence_lo: null,
  confidence_hi: null,
  model_version: null,
  features_contributing: [],
  ...over,
});

describe("score is never presented as a probability", () => {
  it("renders the raw score verbatim with no percent sign (default v1 fixture)", () => {
    const s = fixtureData<{ signal: AnalysisSignal }>("analysis_latest").signal;
    expect(s.probability_calibrated).toBe(false);
    expect(s.probability).toBeNull();

    const d = formatScore(s);
    expect(d.kind).toBe("score");
    expect(d.label).toBe("0.512");
    expect(d.label).not.toContain("%");
    expect(d.detail).toContain("not a probability");
    expect(scoreFieldLabel(s)).toBe("SCORE");
  });

  it("ignores meta.score_is_probability while probability_calibrated is false", () => {
    // semantic/ fixture: meta.score_is_probability === true but the signal is
    // uncalibrated with probability null — the invariant the schema cannot
    // express. The UI must follow probability_calibrated.
    const body = fixtureBody("semantic", "uncalibrated_claims_probability") as {
      data: { signal: AnalysisSignal; meta: { score_is_probability: boolean } };
    };
    expect(body.data.meta.score_is_probability).toBe(true);
    expect(body.data.signal.probability_calibrated).toBe(false);
    expect(body.data.signal.probability).toBeNull();

    expect(formatScore(body.data.signal).kind).toBe("score");
    expect(formatScore(body.data.signal).label).toBe("0.512");
    expect(scoreFieldLabel(body.data.signal)).toBe("SCORE");
    expect(probabilityOrNullText(body.data.signal)).toBe(
      "N/A — CALIBRATION NOT AUTHORISED",
    );
  });

  it("shows a percentage only when the backend calibrated the probability", () => {
    const s = fixtureData<{ signal: AnalysisSignal }>("analysis_latest_calibrated").signal;
    expect(s.probability_calibrated).toBe(true);
    const d = formatScore(s);
    expect(d.kind).toBe("prob");
    expect(d.label).toBe("61.0%");
    expect(scoreFieldLabel(s)).toBe("PROBABILITY");
    expect(probabilityOrNullText(s)).toBe("61.0%");
  });

  it("does not invent a probability when calibration is claimed but the value is null", () => {
    const d = formatScore(signal({ score: 0.44, probability_calibrated: true, probability: null }));
    expect(d.kind).toBe("score");
    expect(d.label).toBe("0.44");
  });

  it("renders a missing score as unavailable, never 0 or 0%", () => {
    const d = formatScore(signal({ score: null }));
    expect(d.kind).toBe("none");
    expect(d.label).toBe(UNAVAILABLE);
    expect(d.label).not.toBe("0");
  });
});

describe("null / missing value handling", () => {
  it("never substitutes a number for a null price or metric", () => {
    expect(fmtPrice(null)).toBe(UNAVAILABLE);
    expect(fmtPrice(undefined)).toBe(UNAVAILABLE);
    expect(fmtNumber(null)).toBe(UNAVAILABLE);
    expect(fmtPrice(2651.05)).toBe("2651.05");
  });

  it("formats UTC timestamps and degrades to '—'", () => {
    expect(formatUtcTime(null)).toBe(UNAVAILABLE);
    expect(formatUtcTime(undefined)).toBe(UNAVAILABLE);
    expect(formatUtcTime("not-a-date")).toBe(UNAVAILABLE);
    expect(formatUtcTime(1760000000)).toBe("2025-10-09 08:53:20 UTC");
    expect(formatUtcTime(1760000000000)).toBe("2025-10-09 08:53:20 UTC");
    expect(formatUtcShort(1760000000)).toBe("08:53");
  });

  it("describes data age without pretending stale data is current", () => {
    expect(ageText(null)).toBe("no data yet");
    expect(ageText(Date.now() - 5_000, Date.now())).toBe("updated 5s ago");
    expect(ageText(Date.now() - 120_000, Date.now())).toBe("updated 2m ago");
  });
});

describe("state tones: unknown is never healthy", () => {
  const row = (over: Partial<TimeframeRow>): Pick<TimeframeRow, "observed" | "quality" | "freshness"> => ({
    observed: true,
    quality: { state: "UNKNOWN", decision_grade: false },
    freshness: null,
    ...over,
  });

  it("maps data-quality states", () => {
    expect(qualityTone(row({ quality: { state: "VALID", decision_grade: true } }))).toBe("up");
    expect(qualityTone(row({ quality: { state: "STALE", decision_grade: false } }))).toBe("warn");
    expect(qualityTone(row({ quality: { state: "INVALID", decision_grade: false } }))).toBe("critical");
    expect(qualityTone(row({ quality: { state: "UNKNOWN", decision_grade: false } }))).toBe("neutral");
    expect(qualityTone(row({ observed: false }))).toBe("neutral");
  });

  it("maps freshness and never upgrades UNKNOWN", () => {
    expect(freshnessTone("FRESH")).toBe("up");
    expect(freshnessTone("STALE")).toBe("warn");
    expect(freshnessTone("UNKNOWN")).toBe("neutral");
    expect(freshnessTone(null)).toBe("neutral");
    expect(freshnessTone(undefined)).toBe("neutral");
  });

  it("maps service / system states including DEGRADED, BLOCKED, RECOVERING", () => {
    expect(serviceStateTone("ONLINE")).toBe("up");
    expect(serviceStateTone("SHADOW")).toBe("accent");
    expect(serviceStateTone("RECOVERING")).toBe("warn");
    expect(serviceStateTone("DEGRADED")).toBe("warn");
    expect(serviceStateTone("BLOCKED")).toBe("critical");
    expect(serviceStateTone("OFFLINE")).toBe("down");
    expect(serviceStateTone("UNKNOWN")).toBe("neutral");
    expect(serviceStateTone(null)).toBe("neutral");
  });

  it("maps aggregate health and direction", () => {
    expect(healthStatusTone("OK")).toBe("up");
    expect(healthStatusTone("DEGRADED")).toBe("warn");
    expect(healthStatusTone("OFFLINE")).toBe("down");
    expect(healthStatusTone(null)).toBe("neutral");
    expect(directionTone("UP")).toBe("up");
    expect(directionTone("DOWN")).toBe("down");
    expect(directionTone("NONE")).toBe("neutral");
    expect(directionTone(null)).toBe("neutral");
    expect(formatDirection(null)).toBe("NONE");
    expect(formatDirection("UP")).toBe("UP");
  });

  it("labels coverage and freshness conservatively", () => {
    expect(coverageLabel("unknown")).toBe("UNKNOWN");
    expect(coverageLabel(null)).toBe("UNKNOWN");
    expect(coverageLabel("high")).toBe("HIGH");
    expect(freshnessLabel(null)).toEqual({ label: "UNKNOWN", tone: "neutral" });
    expect(freshnessLabel(3).label).toBe("FRESH");
    expect(freshnessLabel(3600).tone).toBe("down");
  });
});
