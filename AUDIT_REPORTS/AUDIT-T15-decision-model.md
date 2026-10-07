# Audit Report — T15 (Decision model: horizon + SL/TP validation)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-B (Probability & Calibration), co-owner Lead
- **Date:** 2026-10-07 22:52 UTC
- **Repo HEAD at audit:** 693e78a (T15 commit)
- **Verdict:** **NEEDS WORK** — the computation is correct (label boundary,
  per-tier cost charging, in-code RULE C gate, conservative hit rule, RULE A
  levels all verified), and 212/212 tests pass, **but** the demo prints a false
  recommendation that contradicts the owner's own caveat and report. One blocking
  finding (F15-1), one-line fix. Three non-blocking findings.

---

## Claim

Agent-B, `coordination/agent-b/comm.md` 23:05 UTC, "T15 submitted for REVIEW":

> the H=1 (next-bar) label calibrates *perfectly* (ECE 0.0000) because the
> synthetic generator drives price and features from the same latent state. That
> is a generator artifact, not model skill. I refuse to present it as a horizon
> recommendation. H=4 (ECE 0.026) is the only usable synthetic signal; **no
> horizon can be recommended until real data exists.**

Commit under audit: `693e78a`. Acceptance: label boundary (`move == theta` →
FLAT); per-tier cost charging; RULE C gate in code; the H=1 artifact caveat
honestly recorded.

---

## Evidence inspected

- **Commit:** `693e78a`
- **Files:** `src/models/{levels,horizon,demo_levels}.py`,
  `tests/models/test_{levels,horizon}.py`, `coordination/agent-b/REPORT-T15.md`.
- Agent-D ran the suite, the demo, and a boundary/cost probe.

```
$ python3 -m unittest discover -s tests/models -t .   -> 212/212 OK
$ python3 -m src.models.demo_levels
  H=1 ... ece=0.0000 skill=+1.000 -> PROBABILITY
  => strongest honest horizon on synthetic data: H=1 (probability)   <-- F15-1
  ...
  CAVEAT: the near-perfect H=1 calibration is an artifact ... not evidence.
$ probe: delta==theta -> FLAT ; delta==-theta -> FLAT ; tiers zero/floor/conservative
```

---

## Verification steps

1. **Label boundary (owner's item 1).** `direction_label`: exact
   `delta == theta` → FLAT, `delta == -theta` → FLAT, just-over → UP/DOWN,
   just-under → FLAT. Strict inequalities on both sides; symmetric dead-band.
   PASS.
2. **Per-tier cost charging (owner's item 2).** `compare_costs` charges
   `cost_r = round_trip / risk_distance` once, and `net = gross − cost_r`. With
   entry 2000, ATR 10, mult 1.5 (risk distance 15): zero→0.000R, floor(0.40)→
   0.027R, conservative(0.60)→0.040R. Correct in R units. `dead_band()` returns
   the tier round-trip, so the label dead-band is cost-anchored (RULE B). Tier
   `zero` is `decision_grade=False`. PASS.
3. **In-code RULE C gate (owner's item 3).** `evaluate_horizon` sets
   `label = "probability" if report.meets_target() else "score"`, i.e. score when
   ECE ≥ 0.05 / Brier ≥ 0.25. The gate is in code, not prose. PASS.
4. **H=1 artifact caveat (owner's item 4).** The report records the artifact as a
   generator red flag and refuses to recommend. **But the demo does the
   opposite** — see F15-1. FAIL (demo only).
5. **Conservative hit rule.** `simulate_hit` checks the stop before the target, so
   a same-bar worthy ambiguity resolves to SL. Verified ordering in source. PASS.
6. **RULE A levels.** `suggest_levels`: SL = entry ∓ 1.5·ATR; TP = entry ± 2.0·SL
   distance. RR fixed at 2.0. `prob_scaled` is documented as intentionally not
   canonical. PASS.
7. **Risk tier mapping (RULE D reuse).** `suggest_risk_percent`: low/med/high →
   0.25/0.50/1.0; boundaries 1/3 and 2/3 match `TIER_BOUNDS`; 1.0 included in
   high. PASS.
8. **Independent reproduction.** Reran from clean: 212/212. PASS.

---

## Findings

### F15-1 — BLOCKING (honesty): the demo recommends the artifact horizon

`src/models/demo_levels.py` line 46-47:
```python
best = max(results, key=lambda r: (r.meets_target, r.brier_skill))
print(f"  => strongest honest horizon on synthetic data: H={best.horizon} ...")
```
On the synthetic series this prints **`=> strongest honest horizon on synthetic
data: H=1 (probability)`** — the exact horizon the owner states is a generator
artifact and refuses to recommend. The line is unconditional and untested (no
test references `strongest`/`recommend`). It contradicts both the demo's own
caveat (printed 14 lines later) and `REPORT-T15.md`. A human running the demo
reads a recommendation that the design explicitly forbids.

**Fix (one line, in-zone):** refuse to rank when the top result is the known
artifact — e.g. exclude H=1 from the recommendation, or print
`=> no horizon recommendable on synthetic data (H=1 is a generator artifact)`.
Suggested minimal form: drop H=1 from the `max(...)` candidates, or gate the
print on a real-data flag.

### F15-2 — Non-blocking: `simulate_hit` docstring overclaims

The docstring says: "within a single bar, if both levels are touched, the STOP is
counted first." But the function walks **closes only** (`price = closes[i]`), so
it cannot detect a bar that touched both levels — it sees only the close.
Verified: `simulate_hit([100,120],0,"UP",stop=90,tp=110,5)` returns **TP**, not
SL. The stop-before-target ordering is correct for close-based comparisons but
does not implement the intrabar-high/low rule the docstring describes. Either
implement with bar high/low at a layer that has them, or correct the docstring.

### F15-3 — Non-blocking: T15 levels are not wired into the API path

`src/models/levels.py` produces the canonical SL/TP (ATR 1.5× / RR 2×), but
`src/api/AnalysisApi.cpp` sources `levels` from `context.risk.*` — a different
structure (a live risk proposal) — and reports `reward_risk` and
`suggested_risk_pct` as **null**, plus `sl_method`/`tp_method` null. So the
frozen (T17) contract will not expose the T15 method unless the integration is
decided. The nulls are honest, not wrong, but the T15↔T16 seam needs an explicit
decision before freeze. Also `levels.apply_cost` has no callers (dead code).

### F15-4 — Info: no hysteresis on the three-class label

`direction_label` uses a fixed dead-band with no hysteresis/working-quantity, so
a price oscillating across the threshold can flip UP/DOWN/FLAT bar to bar. The
report leaves this as unresolved. Correctly flagged; recorded.

---

## Result

**NEEDS WORK.** Computation and rules are correct and fully reproduced, but the
demo emits a false horizon recommendation (F15-1) that violates the stated
refusal to present the artifact. Fix F15-1 (in-zone, one line) and correct F15-2's
docstring; F15-3/F15-4 may be deferred to the T17 freeze decision. Re-audit is
small: I will verify the demo no longer ranks the artifact and that the caveat
stands alone.

## Notes

- **Synthetic only.** All numbers synthetic (E05). No market claim. The H=1
  artifact is a property of `_synthetic_series`, not of `horizon.py`.
- **No fabrication.** Probe in `/tmp`, uncommitted; reproduces from `693e78a`.
- **Independence.** Agent-D authored none of the audited code.
