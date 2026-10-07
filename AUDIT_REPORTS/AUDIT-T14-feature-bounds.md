# Audit Report — T14 (Feature bounds / NaN-inf guards)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-A (Features & Analytics)
- **Date:** 2026-10-07 22:20 UTC
- **Repo HEAD at audit:** 8b56865 (T14 commit)
- **Verdict:** **PASS.** Every feature is finite and inside its declared range on
  all tested edge cases; the guards map non-finite input to a bounded value, and
  pathological input is correctly flagged INVALID/INCOMPLETE rather than silently
  accepted. T14 may go to **DONE** at the Lead's confirmation.

---

## Claim

Agent-A, `coordination/agent-a/comm.md` 22:13 UTC, "Requested: T14 audit":

> every field's range in AnalyticalFeatures.h is now asserted, and no field is
> ever NaN/inf … deterministic edge battery × all 9 timeframes; fixed-seed LCG
> sweep 9 TFs × 40 seeds; insufficient history → !valid/non-VALID; zero-range →
> INVALID with reason; 1e12 gap → finite; cross block bounded for full/missing/
> degenerate; computeAll bounded incl. absent streams.
> Evidence: 31 feature cases green; CTest 12/12; warning-free.

Commit under audit: `8b56865` "agent-a: T14 bounds/NaN-inf guards (8 cases) +
FEATURES.md interpretability index".

Acceptance: deterministic, bounded (no NaN/inf), interpretable, tested.

---

## Evidence inspected

- **Commit:** `8b56865`
- **Files:** `tests/features/AnalyticalFeatureBoundsTests.cpp`,
  `src/analysis/features/{FEATURES.md,AnalyticalFeatures.h,AnalyticalFeatureEngine.cpp}`.
- Agent-D reran the bounds battery and added an independent pathological-input
  probe (NaN/inf/huge/negative/zero/denormal bars).

```
$ g++ ... AnalyticalFeatureBoundsTests.cpp ... && /tmp/feat_bounds
8 test case(s), 0 failed   RESULT: PASS
$ g++ ... /tmp/t14_probe.cpp ... && /tmp/t14_probe
adversarial bounds probe: 8 suites, 0 violations
RESULT: PASS (all fields finite & in range)
$ ctest --test-dir build   -> 13/13 passed
```

---

## Verification steps

1. **Reran the 8 bounds cases.** All pass.
2. **Independent pathological probe (Agent-D).** Fed 8 suites of hostile bars —
   all-NaN, ±inf, ±1e300, negative prices, single bar, zero prices, all-zero
   prices, denormal 1e-300 — through `computeTimeframe`, `computeCross`, and
   `computeAll`. **0 range violations**: every signed field in [-1,1], every unit
   field in [0,1], every ternary field in {-1,0,1}, all finite.
3. **Guard mechanism.** `clampSigned`/`clampUnit` map any non-finite input to
   0.0 and clamp to range (`AnalyticalFeatureEngine.cpp:16,23`). This is why the
   invariants hold even for NaN/inf inputs.
4. **Honesty — bounded garbage is not reported as valid.** Pathological suites are
   correctly flagged: NaN/inf/zero-range → `quality=INVALID` (2), detail
   "degenerate window: zero high-low range"; 1-bar → `quality=INCOMPLETE` (8),
   detail "insufficient trigger bars: 1 < 3". Only the genuine negative-price
   series (a valid mirror market) is `valid=1, quality=VALID`. The guards do not
   launder bad data into a valid result.
5. **Interpretability index accurate.** FEATURES.md's `name — range — file:line`
   index spot-checked against source: `bodyRatio:170`, `upperWickRatio:171`,
   `lowerWickRatio:173`, `higherHighShare:186`, `rangePosition:191`,
   `swingAsymmetry:201`, `netChangeRatio:255`, `patternScore:256`,
   `contextRangePosition:270` all match. No fabricated line references.
6. **No regression.** `ctest` 13/13 pass; RULE-A and unit/leakage suites still
   green (rerun earlier this session).
7. **Header/doc comment corrected.** `asOfBarOpenSec` now documented as "decision
   instant this vector describes" (consistent with the T01 common-instant fix).
8. **Zone.** `8b56865` touched `tests/features/` and
   `src/analysis/features/` — in-zone for Agent-A.

---

## Result

**PASS.** Bounds and NaN/inf guards are correct and independently reproduced on
adversarial input; invalid data is flagged, not silently bounded-and-accepted; the
interpretability index is accurate. T14 may go to **DONE**.

## Notes

- **No fabrication.** Probe in `/tmp`, uncommitted; reproduces from `8b56865`.
- **Design note (not a defect).** Clamping non-finite to 0.0 means a feature value
  of 0.0 is ambiguous between "genuinely neutral" and "was non-finite". The
  INVALID/INCOMPLETE quality flag is the disambiguator and is set correctly for
  the pathological cases tested; a consumer must read `quality`/`valid`, not the
  value alone. Worth keeping in mind for the T13 end-to-end integration.
- **Independence.** Agent-D authored none of the audited code.
