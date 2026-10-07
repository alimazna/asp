# Audit Report — T21 (Integration causality test)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-A (Features & Analytics)
- **Date:** 2026-10-07 22:46 UTC
- **Repo HEAD at audit:** 62b9a2f (test commit; also e6c3045 tip)
- **Verdict:** **PASS.** The causality equivalence is correct and, critically, the
  test has real mutation-detection power: it fails if the decision-instant cutoff
  is ignored. The feature suites are now registered with CTest (E02 fix verified).

---

## Claim

Agent-A, `coordination/agent-a/comm.md` 22:44 UTC, "T21 submitted":

> `interior_instant_equals_truncated_prefix_across_streams`. It proves that
> `computeAll` pinned to an interior instant on **unequal-length** M15/H4/D1
> streams is byte-equal to `computeAll` over the same streams truncated to that
> instant's prefix — the exact equivalence T13 integration depends on.

Test at `tests/features/AnalyticalFeatureLeakageTests.cpp`, landed `62b9a2f`.
Acceptance: the equivalence holds on unequal-length streams, and the test is
causal (a lookahead would break it).

---

## Evidence inspected

- **Commit:** `62b9a2f` (test); CTest registration from E02 fix `c419eca`.
- **Files:** `tests/features/AnalyticalFeatureLeakageTests.cpp`,
  `src/analysis/features/AnalyticalFeatureEngine.{h,cpp}`.
- Independent probe: `/tmp/t21_probe.cpp`.

```
$ ctest --test-dir build -R AnalyticalFeature       -> 4/4 passed
  AnalyticalFeatureBoundsTests / LeakageTests / RuleATests / Tests
$ /tmp/t21_probe
T21 sweep: instants=44 cross/feat mismatches=0 future-bar-reads=0 -> PASS
future-bar insensitivity at pinned instant: cross=1 feat=1 -> PASS
```

---

## Verification steps

1. **Equivalence reproduced, broadly.** The owner's case pins one interior H4
   instant. I swept **every** H4 instant (44 of them, interior and boundary) on
   unequal-length M15(48)/H4(44)/D1(40) streams, comparing `computeAll(all, t)`
   against `computeAll(truncateAt(all, t), t)` field-by-field (all 25
   `TimeframeFeatures` fields + all 11 `CrossTimeframeFeatures` fields):
   **0 mismatches.** PASS.
2. **No future bars read (causality).** For each pinned instant I checked that
   every stream's `barsAvailable` equals the number of bars with
   `openTimeSec <= t`. **0 future-bar reads.** The cutoff in `computeTimeframe`
   (`remove_if b.openTimeSec > asOfBarOpenSec`) is the mechanism. PASS.
3. **Mutation-detection power (the key check).** A test that only compares two
   calls that both read the same data can pass vacuously. I pinned an interior
   instant, then **appended 40 future bars** to every stream and recomputed at the
   same instant: the cross and per-timeframe features are **identical**. If the
   cutoff were ignored, the appended future bars would change the result and the
   test would fail. So the test genuinely detects a lookahead. PASS.
4. **Unregistered-test gap closed (E02).** `ctest -N` now lists 18 tests
   including the four feature suites (#15–#18); full `ctest` = 18/18. The T21 case
   executes in `AnalyticalFeatureLeakageTests` (8 cases). PASS.
5. **Independence.** Reproduced by compiling and running the registered suite and
   by an independent probe; did not trust the claimed pass. PASS.

---

## Result

**PASS.** The equivalence holds across all instants, no future bar is read, and
the test is causal — it would catch a regression that ignored the decision instant.

## Notes

- **N1 — Scope.** This underwrites T13 (integration) but is not itself an
  end-to-end integration test; T13 remains the integration task.
- **N2 — Synthetic.** Deterministic synthetic series; no real-data claim.
- **N3 — E02 provenance.** The registration fix was a Lead-authorised one-line
  `GLOB → GLOB_RECURSE` (`c419eca`); the test only became CI-visible because of it.
