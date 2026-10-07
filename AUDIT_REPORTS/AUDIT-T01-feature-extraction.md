# Audit Report — T01 (Feature extraction) — LEAKAGE

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-A (Features & Analytics)
- **Date:** 2026-10-07 21:24 UTC
- **Repo HEAD at audit:** 368adf3a21d0e769582293abd3e4b4cbc8a1e36a (commit under audit)
- **Verdict:** **FAIL — must return to Agent-A.** Two causality/alignment defects
  (F1, F2) violate RULE 4 (no lookahead). The per-timeframe feature maths and its
  boundedness/honesty properties are otherwise sound. T01 must **not** move to
  DONE. See §Required fixes for the minimal change.

---

## Claim

Agent-A, `coordination/agent-a/comm.md` 21:18 UTC, "@agent-d — T01 ready for
audit — Feature extraction" (`Reply required: yes`), claimed:

> Deliverables (zone: src/analysis/features/, tests/features/):
>   - AnalyticalFeatures.h / AnalyticalFeatureEngine.{h,cpp} / FEATURES.md
>   - tests/features/AnalyticalFeatureTests.cpp (9 cases)
>   - tests/features/AnalyticalFeatureLeakageTests.cpp (5 cases)
> Evidence ... No-lookahead: engine takes an explicit `asOfBarOpenSec` and drops
> every bar with a later open time before computing; leakage tests prove that
> appending, mutating, or replacing future bars does not change a feature
> computed as of T. Boundedness: every feature clamped to [-1,1] or [0,1].
> Honesty: insufficient/absent input -> INCOMPLETE/INVALID/UNKNOWN, never faked.
> Please audit: leakage, boundedness, determinism, and that H4 is treated as
> structural authority and M15 as the operational trigger.

Commit under audit: `368adf3` "agent-a: T01 analytical feature layer
(9 TF + cross-TF, causal, bounded) + tests" (2026-10-07 21:17:02 +0000).

Claimed acceptance criteria (`tasks.md` T01/T02 notes): feature extraction over
3 months × 9 timeframes plus the latest 9 closed candles; tests must prove no
lookahead (RULE 4) and no reward-structure artifact (RULE A).

---

## Evidence inspected

- **Commit:** `368adf3a21d0e769582293abd3e4b4cbc8a1e36a`
- **Files:** `src/analysis/features/AnalyticalFeatures.h`,
  `AnalyticalFeatureEngine.{h,cpp}`, `FEATURES.md`,
  `tests/features/AnalyticalFeature{Tests,LeakageTests}.cpp`
- **Agent-D reran everything** (not trusted from the claim) and added two
  adversarial probes of its own.

```
$ cmake --build build -j4          # core rebuilt to include AnalyticalFeatureEngine
$ g++ -std=c++17 -I src -I tests -Wall -Wextra -o /tmp/feat_unit  tests/features/AnalyticalFeatureTests.cpp         build/libaura_core.a -lpthread
$ g++ -std=c++17 -I src -I tests -Wall -Wextra -o /tmp/feat_leak  tests/features/AnalyticalFeatureLeakageTests.cpp  build/libaura_core.a -lpthread
$ /tmp/feat_unit
9 test case(s), 0 failed, 0 assertion failure(s)   RESULT: PASS
$ /tmp/feat_leak
5 test case(s), 0 failed, 0 assertion failure(s)   RESULT: PASS
```

So the **supplied** 14 cases pass. The defects below are cases the supplied
tests do **not** cover.

---

## Verification steps

Adversarial: default assumption is that the feature path peeks at the future
until proven otherwise. Each claim was reproduced or falsified by direct
execution.

1. **Per-timeframe causality (`computeTimeframe`) — PASS.** The engine drops
   every bar with `openTimeSec > asOfBarOpenSec` before computing
   (`AnalyticalFeatureEngine.cpp`). Reproduced the 3 leakage cases; a future-bar
   append/mutation does not change the result. Sound.
2. **Boundedness — PASS.** Every feature is routed through `clampSigned`
   [-1,1] / `clampUnit` [0,1]; `clamp*` also maps non-finite to 0. `FEATURES.md`
   formula/range table matches the code. Sound.
3. **Determinism — PASS.** No randomness, no wall-clock, no unordered iteration.
   `features_are_deterministic` passes; re-running produced identical output.
4. **H4 authority / M15 trigger — PASS (as wiring).** `computeCross` maps
   `h4StructuralAuthority <- H4.structureTrend`, `m15TriggerState <- M15`, and
   requires both present for `valid`. Matches V4-04 / DEC-011.
5. **9-candle window — PASS.** `kTriggerWindow = 9`; `windowUsed` capped at 9.
6. **Cross-timeframe causality — FAIL (F1).** See below.
7. **Common decision instant across timeframes — FAIL (F2).** See below.
8. **No reward-structure artifact (RULE A) — PASS.** No target/stop or
   win-rate construction in the feature layer; features are descriptive only.
9. **Honesty (no fabrication) — PASS.** Insufficient/absent input yields
   INCOMPLETE/INVALID/UNKNOWN with a `detail`; absent streams are UNKNOWN, not
   invented.
10. **Zone check.** `368adf3` touched only `src/analysis/features/` and
    `tests/features/` (+ coordination files) — inside Agent-A's declared zone.
    No production/frozen files touched. Clean.

---

## F1 — `computeCross` has no decision-time pin (causality defect)

**Severity: high (RULE 4).**

`computeCross(byTimeframe)` has **no `asOfBarOpenSec` parameter** (header lines
30–41 provide it only on `computeTimeframe`). It calls
`computeTimeframe(stream)` with the default `-1`, so each stream is pinned to
**its own last supplied bar**, and `c.asOfBarOpenSec = m15.asOfBarOpenSec` — the
M15 tail. A live feed appending new M15 bars therefore moves the cross features.

Agent-D's adversarial probe (append 20 future M15 bars after computing once):

```
before: m15Avail=1 h4Avail=1 m15Trigger=0.976316 h4M15Agree=1.0 asOf=35100
after : m15Avail=1 h4Avail=1 m15Trigger=0.916667 h4M15Agree=1.0 asOf=53100
FUTURE-M15 CHANGED computeCross OUTPUT: YES (LEAK)
```

`asOf` jumped 35100 -> 53100 and `m15TriggerState` changed 0.976 -> 0.917 purely
because future bars were appended. This is exactly the lookahead RULE 4 forbids.

**Why the supplied leakage test misses it.** `cross_timeframe_is_causal`
(`AnalyticalFeatureLeakageTests.cpp`) mutates only the **D1** stream, then asserts
`h4M15Agreement` / `h4StructuralAuthority` / `m15TriggerState` are unchanged —
which they are, because those read M15/H4. The test never appends or mutates
**M15 or H4**, so it cannot detect the leak. Its comment ("computeCross uses the
M15 tail") actually documents the hazard it fails to test.

## F2 — no common decision instant across the 9 timeframes (alignment defect)

**Severity: high (RULE 4).**

`computeAll(byTimeframe)` also has no `asOfBarOpenSec` parameter and calls
`computeTimeframe(stream)` per stream with the default `-1`. With realistically
different stream lengths, each timeframe is computed at a **different** decision
bar. Agent-D's probe (all 9 streams supplied, differing lengths):

```
set.asOf(cross)=179100  set.valid=1
  tf=M1   asOf=17940     windowUsed=9 valid=1
  tf=M5   asOf=89700     windowUsed=9 valid=1
  tf=M15  asOf=179100    windowUsed=9 valid=1
  tf=M30  asOf=358200    windowUsed=9 valid=1
  tf=H1   asOf=536400    windowUsed=9 valid=1
  tf=H4   asOf=1713600   windowUsed=9 valid=1
  tf=D1   asOf=7689600   windowUsed=9 valid=1
  tf=W1   asOf=23587200  windowUsed=9 valid=1
  tf=MN1  asOf=59616000  windowUsed=9 valid=1
```

`set.valid` is **true** while the per-timeframe `asOf` values span
17940 -> 59616000 (M1 sees ~17940; MN1 sees ~59616000). A "multi-timeframe"
feature set whose members describe different instants is not a single decision
snapshot; downstream cross-timeframe comparisons are then meaningless and, when
`asOf` defaults to the tail, non-causal.

**Minimal fix (Agent-A, in zone).** Thread one explicit
`std::int64_t asOfBarOpenSec` (default -1) through **both** `computeCross` and
`computeAll`, and pass it to every `computeTimeframe(..., asOfBarOpenSec)`
call, so all nine streams and the cross block share the decision bar. Then add
the missing adversarial tests: append/mutate **M15 and H4** future bars and
assert the cross output is unchanged; and assert a single common `asOf` across
all nine per-timeframe vectors. This is a small, additive change.

---

## Result

**FAIL — return to Agent-A.** Per-timeframe feature maths, boundedness,
determinism, honesty, H4/M15 wiring, and zone hygiene are correct. Two causality
defects (F1, F2) fail the RULE 4 acceptance criterion for a multi-timeframe
feature set. T01 stays **REVIEW**; Agent-D does not mark it DONE. The Lead owns
the status change.

## Required fixes (checklist for re-audit)

1. Add `asOfBarOpenSec` to `computeCross` and `computeAll`; pin all streams to it.
2. Add leakage tests that append/mutate **M15/H4** future bars (not just D1).
3. Add a test asserting one common `asOf` across all nine per-timeframe vectors.
4. Re-run and re-submit; Agent-D will re-audit the two items only.

## Notes

- **No fabrication by the auditor.** Findings are reproducible from the two
  probes; the probe sources are ephemeral (in `/tmp`) and intentionally not
  committed.
- **Scope/evidence limits.** Verified against synthetic bar series only; no real
  MT5 data was available. The defects are structural (missing parameter / no
  common instant) and independent of data source.
- **Process note.** Agent-A's 21:18 UTC request flags a separate build-graph
  issue: `CMakeLists.txt` uses a non-recursive glob, so `tests/features/` is not
  compiled into CTest (its tests were run manually). Agent-D reproduced them by
  manual compile. This is the same class of gap as Agent-D's F2 on T06 and
  should be resolved once by the Lead (recursive glob or explicit test
  registration).

---

## ADDENDUM A — re-audit after Agent-A fix (2026-10-07 21:35 UTC)

- **Re-audited commit:** `60d04cbded05d1af96c7b0098da1d37699e33cf0`
  ("agent-a: fix T01/T10 causality (common decision instant in
  computeCross/computeAll) + leakage tests").
- **Verdict: F1/F2 FIXED. T01 now PASSES the causality criterion.**

What changed (read from the diff, not the claim):

- `computeCross` and `computeAll` now take `asOfBarOpenSec` (default -1) and
  thread one `asOf` into every `computeTimeframe(..., asOf)` call.
- When unpinned, the default is `latestOpenAcross(byTimeframe)` — the max
  observed bar open time across all streams — **not** a per-stream tail.
- `computeTimeframe` now sets `f.asOfBarOpenSec` to the decision instant (not the
  last-read bar) and erases bars with `openTimeSec > asOfBarOpenSec`.
- `computeCross` reports `c.asOfBarOpenSec = asOf` (the common instant, not the
  M15 tail); `computeAll` sets `set.asOfBarOpenSec = asOf`.

Independent re-verification (Agent-D, not Agent-A's tests):

```
$ cmake --build build -j4 ; g++ ... feat_unit2 ; g++ ... feat_leak2
UNIT    9/9 PASS
LEAKAGE 7/7 PASS
$ ctest --test-dir build        12/12 PASS
$ g++ -Wall -Wextra -c AnalyticalFeatureEngine.cpp   (warning-free)

Adversarial probe (Agent-D):
PART A (pinned asOf=35100):
  before asOf=35100 m15Trigger=0.976316
  after  asOf=35100 m15Trigger=0.976316
  PINNED LEAK: no
PART B (unpinned, 9 equal streams):
  set.asOf == cross.asOf; all per-TF share set.asOf: YES
  after future-M15 append: all-TF-share=YES cross-agrees=YES
  UNPINNED DRIFT: no
OVERALL: F1/F2 FIXED (invariants hold)
```

- **F1 closed.** With a pinned instant the cross output is invariant to future
  M15 bars (the exact probe that previously leaked now returns 0.976316 both
  times).
- **F2 closed.** Unpinned, every per-TF vector, the cross vector, and the set
  share one `asOfBarOpenSec`; no per-stream drift.
- **New leakage tests are the right shape.** `cross_future_m15_h4_bars_do_not_leak`
  appends future **M15 + H4** bars (the previous gap), and
  `compute_all_shares_one_decision_instant` asserts the common instant. Both
  pass; both fail against the pre-fix engine.

Residual observations (not blockers):

- **N1 (advisory, RULE 4 wording).** When unpinned, the default instant is the
  latest bar across streams; appending a future bar legitimately advances the
  set instant (correct causal behaviour — it is what a live feed does). The
  caller must therefore pin `asOf` for a reproducible historical snapshot. The
  new tests cover the pinned case; consider documenting the pinned/unpinned
  contract in `FEATURES.md`. Not a defect.
- **N2 (build graph, unchanged).** `tests/features/` is still outside CTest
  (non-recursive glob). Agent-A's tests were rerun manually. Awaiting the
  Lead/human build-graph decision.

**Status: T01 → PASS (causality).** No further code change requested. Agent-D
does not set DONE; the Lead owns the status change.

## Notes (Addendum A)

- **No fabrication.** Every addendum claim reproduces from `60d04cb`; the probe
  is in `/tmp` and uncommitted.
- **Scope.** Synthetic bars; pinned + unpinned cases both exercised.
