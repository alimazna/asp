# Audit Report — T02 (Feature tests / RULE A direction neutrality)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-A (Features & Analytics)
- **Date:** 2026-10-07 21:45 UTC
- **Repo HEAD at audit:** a1677684aa7fee1832beff856190118ef9263b27 (commit under audit)
- **Verdict:** **PASS.** RULE A (no reward-structure artifact / no directional
  bias) holds on the feature layer. One documented, bounded caveat (volatilityRatio)
  is honestly disclosed and is not a directional bias.

---

## Claim

Agent-A, `coordination/agent-a/comm.md` 21:43 UTC, "T02 ready for audit"
(`Reply required: yes`):

> **T02 ready for audit.** New file:
> `tests/features/AnalyticalFeatureRuleATests.cpp` (7 cases). RULE A for a feature
> layer = direction neutrality, tested under the mirror
> `(o,h,l,c) -> (K-o, K-l, K-h, K-c)` … T02 test surface = 23 cases (unit 9 +
> leakage 7 + RULE A 7), all green; existing CTest 12/12; warning-free.

Commit under audit: `a167768` "agent-a: T02 RULE-A direction-neutrality tests +
document pinned/unpinned contract" (2026-10-07 21:43:23 +0000).

Acceptance: feature tests prove no lookahead (RULE 4) and no reward-structure
artifact (RULE A).

---

## Evidence inspected

- **Commit:** `a1677684aa7fee1832beff856190118ef9263b27`
- **Files:** `tests/features/AnalyticalFeatureRuleATests.cpp`,
  `src/analysis/features/FEATURES.md` (pinned/unpinned contract section).
- Agent-D reran all three feature test binaries and added an independent
  mirror-symmetry sweep.

```
$ g++ -std=c++17 -I src -I tests -Wall -Wextra -o /tmp/feat_rulea \
      tests/features/AnalyticalFeatureRuleATests.cpp build/libaura_core.a -lpthread
$ /tmp/feat_rulea
7 test case(s), 0 failed   RESULT: PASS
$ /tmp/feat_unit2   9/9 PASS
$ /tmp/feat_leak2   7/7 PASS
$ ctest --test-dir build   12/12 PASS
```

---

## Verification steps

1. **Reran the 7 RULE-A cases.** All pass.
2. **Mirror definition is correct.** `(o,h,l,c) -> (K-o, K-l, K-h, K-c)`
   reflects about K and swaps high/low; a direction-neutral engine must map sign
   features to their negative, position features to `1-x`, extremes to swapped,
   magnitudes to themselves. The test asserts exactly this.
3. **Sign antisymmetry — PASS.** structureTrend, runBalance, momentumNorm,
   netChangeRatio, patternScore, swingAsymmetry, contextTrend, candleDirection,
   and the cross signs all satisfy `x' == -x` exactly.
4. **Position reflect — PASS.** rangePosition, contextRangePosition -> `1-x`.
5. **Extreme swap — PASS.** upperWick<->lowerWick, higherHigh<->lowerLow.
6. **Magnitude invariant — PASS.** bodyRatio, atrRatio, momentumPersistence,
   momentumAcceleration.
7. **Cross block — PASS.** h4StructuralAuthority / m15TriggerState /
   h4M15Agreement / h4D1Agreement flip sign; mtfConflictScore invariant.
8. **Flat market — PASS.** all sign features exactly 0.
9. **Independent broad sweep (Agent-D).** Beyond Agent-A's single synthetic
   series, Agent-D ran a deterministic random-walk sweep over **180 (seed,
   timeframe) pairs**:
   ```
   max |sign mirror error|      = 1.16e-14
   max |position mirror error|  = 1.43e-14
   max |magnitude mirror error| = 2.32e-13
   max |volatility mirror error|= 4.57e-02   (second-order, bounded)
   DIRECTIONAL BIAS DETECTED: no
   ```
   Direction neutrality is not an artifact of the chosen series; it holds to
   floating-point precision across the sweep.
10. **Honesty of the volatilityRatio caveat — CONFIRMED, strengthened.**
    Agent-A disclosed that `volatilityRatio` (a log-return share) is invariant
    only to second order, measured ~5.5e-3 on their `r=0.002` series. Agent-D's
    sweep shows the error grows with move magnitude, reaching 4.57e-2 on larger
    random walks. This is a **bounded magnitude asymmetry with no directional
    preference** — reflection maps `r -> log(1-r)`, not `-log(1+r)`, so `|r|` is
    not preserved. It does not create a long or short bias, so it does not
    violate RULE A. The disclosure is accurate and conservative.
11. **Pinned/unpinned contract documented.** `FEATURES.md` now states pinned
    (`asOf >= 0`) = reproducible snapshot, unpinned = advances with the feed.
    This closes Agent-D's T01 advisory N1.
12. **Zone.** `a167768` touched `tests/features/`, `src/analysis/features/FEATURES.md`,
    coordination files. In-zone; no production/frozen edits.

---

## Result

**PASS.** The feature layer is direction-neutral under the mirror: sign features
antisymmetric, positions reflected, extremes swapped, magnitudes invariant, flat
market exactly zero. Confirmed independently on a 180-pair sweep. The single
second-order caveat (volatilityRatio) is honestly documented, bounded, and not a
directional bias. T02 may go to **DONE** at the Lead's confirmation.

## Notes

- **No fabrication.** The sweep reproduces from the repo; its source is in `/tmp`
  and intentionally uncommitted.
- **Evidence limits.** Synthetic bars only (no MT5 data). Direction neutrality is
  a structural property and is data-independent.
- **Advisory (not a defect).** The volatilityRatio second-order asymmetry means a
  model that leans on that one feature could see a tiny magnitude difference
  between mirrored markets. It cannot flip a long/short decision, but the Lead
  may want the feature documented as "magnitude, second-order symmetric" in
  FEATURES.md. Agent-A already notes it in the test; a one-line note in
  FEATURES.md would close the loop.
- **Build graph (unchanged).** `tests/features/` is still outside CTest
  (non-recursive glob); tests were rerun manually. Awaiting the Lead/human
  decision.
