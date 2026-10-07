# Audit Report — T10 (Leakage audit of T01 feature layer)

- **Auditor:** Agent-D (Verification & Audit)
- **Subject under audit:** T01 feature layer, commit `368adf3a21d0e769582293abd3e4b4cbc8a1e36a`
  (owner Agent-A)
- **Reviewer of this audit:** Lead (DeepSeek)
- **Date:** 2026-10-07 21:27 UTC
- **Repo HEAD at audit:** 7e7a752 (feature layer unchanged since 368adf3)
- **Verdict:** **FAIL — LEAKAGE CONFIRMED in the cross-timeframe path.** The
  per-timeframe path is clean; the cross/aggregate paths read the future. T10
  cannot pass while F1/F2 stand.

This is the formal T10 leakage audit. It supersedes and consolidates the
leakage portion of `AUDIT_REPORTS/AUDIT-T01-feature-extraction.md` (the same
evidence, viewed specifically through the RULE 4 leakage lens). T01 and T10 are
auditing the same artefact.

---

## Scope

RULE 4 (no lookahead): no feature may depend on a bar that closes after the
decision instant. RULE A (no reward-structure artifact): the feature layer must
not encode a target/stop or win-rate construction that manufactures an edge.

Audited surface: `src/analysis/features/` (T01). Method: read the code, rerun
Agent-A's 14 supplied cases, then apply adversarial probes the supplied tests do
not contain.

---

## Evidence

```
$ /tmp/feat_unit    # AnalyticalFeatureTests.cpp
9 test case(s), 0 failed   RESULT: PASS
$ /tmp/feat_leak    # AnalyticalFeatureLeakageTests.cpp
5 test case(s), 0 failed   RESULT: PASS

# Adversarial probe (Agent-D): append 20 FUTURE M15 bars, recompute cross
before: m15Avail=1 h4Avail=1 m15Trigger=0.976316 h4M15Agree=1.0 asOf=35100
after : m15Avail=1 h4Avail=1 m15Trigger=0.916667 h4M15Agree=1.0 asOf=53100
FUTURE-M15 CHANGED computeCross OUTPUT: YES (LEAK)

# Adversarial probe (Agent-D): 9 streams of differing lengths, computeAll
set.asOf(cross)=179100  set.valid=1
  tf=M1  asOf=17940   | tf=M15 asOf=179100  | tf=H4 asOf=1713600 | tf=MN1 asOf=59616000
  ... (span 17940 .. 59616000, all valid)
```

---

## Leakage findings

### L1 — `computeCross` reads the future (FAIL, high)

`computeCross` has no `asOfBarOpenSec` parameter; it pins M15/H4/D1 to each
stream's own last supplied bar (`computeTimeframe(..., -1)`), and reports
`c.asOfBarOpenSec = m15.asOfBarOpenSec` (the M15 tail). Appending future M15
bars changes `m15TriggerState` and `asOf`. Demonstrated above. This is
lookahead: a feature "as of" decision time T silently advances to T+k when new
bars arrive.

### L2 — `computeAll` has no common decision instant (FAIL, high)

`computeAll` likewise omits `asOf`; with unequal stream lengths each timeframe
is computed at a different bar, yet `set.valid` is true. The M15 stream reaches
17940 while MN1 reaches 59616000 — the "feature set" is not one snapshot, and
the cross block is pinned to the M15 tail. Demonstrated above.

### Why the supplied leakage suite passes anyway

`cross_timeframe_is_causal` mutates only **D1**, then asserts M15/H4-derived
fields are unchanged. It never appends or mutates **M15 or H4**, so it cannot
detect L1/L2. The test's own comment ("computeCross uses the M15 tail")
describes the hazard it does not test. This is a test-coverage gap, not a false
claim: Agent-A's prose ("engine takes an explicit asOfBarOpenSec and drops every
bar with a later open time") is true of `computeTimeframe` and false of
`computeCross`/`computeAll`.

---

## What is clean

- **Per-timeframe path (`computeTimeframe`).** Causal: bars with
  `openTimeSec > asOfBarOpenSec` are dropped before any computation. All 3
  per-timeframe leakage cases reproduce.
- **Determinism.** No wall-clock, randomness, or unordered iteration in
  `src/analysis/features/` (grep confirmed). Identical input -> identical output.
- **RULE A.** No target/stop/win-rate construction anywhere in the layer;
  features are descriptive and bounded. Clean.
- **Boundedness / honesty.** clamp to [-1,1]/[0,1]; insufficient/absent input ->
  INCOMPLETE/INVALID/UNKNOWN with a reason; absent streams UNKNOWN, not faked.

---

## Result

**FAIL — LEAKAGE CONFIRMED.** T10 cannot be marked DONE. The leakage is confined
to the cross/aggregate entry points; the per-timeframe engine is sound.

**Exit condition for T10 to pass (Agent-A, in zone):**
1. Add `asOfBarOpenSec` to `computeCross` and `computeAll`; pin every stream.
2. Add a leakage test that appends/mutates **M15 and H4** future bars.
3. Add a test asserting one common `asOf` across all nine vectors.
4. Re-submit; Agent-D re-runs T10 and closes it.

## Notes

- **Reproducibility.** Findings reproduce from the repo alone (two probes in
  `/tmp`, intentionally uncommitted). No production code was modified.
- **Evidence limits.** Synthetic bars only (no MT5 data). The defect is
  structural and data-independent.
- **Independence.** Agent-D authored none of the audited code.
