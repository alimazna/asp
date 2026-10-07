# Audit Report — T20 (Three cost tiers, RULE B)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-B (Probability & Calibration)
- **Date:** 2026-10-07 23:02 UTC
- **Repo HEAD at audit:** 0d9e64b (T20 commit); method verified on 8265401
- **Verdict:** **PASS.** The three tiers are correct, ordered, and correctly
  flagged; validation rejects negatives; `cost_r`/`net_expectancy_r` are correct;
  the `levels.py` refactor is a true deduplication with identical values. One
  non-blocking robustness finding (F20-1). RULE B is now satisfiable.

---

## Claim

Agent-B, `coordination/agent-b/comm.md` (T20 submission):

> `src/models/costs.py` — NEW. `CostAssumptions`, `CostTier`, `cost_tiers`,
> `tier_by_name`, `cost_r`, `net_expectancy_r`. `src/models/levels.py` refactored
> onto it (deduplicated). Zone note: T20's assigned deliverable was `src/costs/`,
> but my zone is `src/models/`; I did **not** write outside it and put the model in
> `src/models/costs.py`.

Commit: `0d9e64b`. Acceptance (RULE B): exactly three tiers; `zero` reference-only
and never decision-grade; per-tier cost honest; the dead-band is cost-anchored.

---

## Evidence inspected

- **Commit:** `0d9e64b`
- **Files:** `src/models/costs.py`, `src/models/levels.py`,
  `tests/models/test_costs.py`, `coordination/agent-b/REPORT-T20.md`.
- Independent probe with validation edge cases.

```
$ python3 -m unittest discover -s tests/models -t .   -> 225/225 OK
$ probe
tiers: [('zero',0.0,False), ('floor',0.4,True), ('conservative',0.6,True)]
all-negative -> rejected ; -inf -> rejected
cost_r(conservative, risk=3) = 0.200
net(0.75,0.2)=0.55 ; net(0.1,0.2)=-0.10
levels.COST_TIERS == costs.cost_tiers()   (identical values)
```

---

## Verification steps

1. **Three tiers, RULE B (owner's item 1).** `cost_tiers()` returns exactly
   `zero (0.00, reference)`, `floor (0.40 = spread 0.30 + commission 0.10)`,
   `conservative (0.60 = + slippage 0.20)`. Strictly increasing. `decision_grade`
   is `False` only for `zero`. Matches MISSION §10.6 / E04 ("spread 0.30 +
   commission, plus slippage"). PASS.
2. **Validation (owner's item 2).** Negative spread/commission/slippage rejected
   (`SplitError`); `-inf` rejected; zero allowed; `cost_r` rejects
   `risk_distance <= 0`. PASS.
3. **`decision_grade` flags (owner's item 3).** `zero` reference-only; `floor` and
   `conservative` decision-grade. `dead_band() == round_trip`. PASS.
4. **Cost arithmetic.** `cost_r(conservative, risk=3.0) = 0.60/3.0 = 0.200`;
   `net_expectancy_r` = gross − cost; sign correct (0.75→+0.55, 0.10→−0.10). PASS.
5. **Deduplication (refactor safety).** `levels.py` no longer defines its own
   SPREAD/COMMISSION/SLIPPAGE; it re-exports `COST_TIERS = cost_tiers()` from
   `costs.py`. Values are byte-identical to the pre-refactor constants
   (0.0/0.4/0.6) and to `costs.cost_tiers()`. No duplication, no drift. PASS.
6. **In-zone compliance (owner's item 4).** T20's nominal path was `src/costs/`;
   Agent-B stayed in `src/models/`. I verified no file outside `src/models/` and
   `tests/models/` was touched by `0d9e64b`. The zone rule was honoured. The path
   decision (keep `src/models/costs.py` vs move to `src/costs/`) is a Lead call;
   **the code is correct either way** and the mission's RULE B numbers now exist.
   PASS (with the Lead's path ruling pending, not an audit blocker).
7. **Independent reproduction.** Reran from clean: 225/225. PASS.

---

## Findings

### F20-1 — Non-blocking (robustness): NaN / +inf assumptions are accepted

`CostAssumptions.validate()` checks only `value < 0.0`. For `NaN`, `NaN < 0` is
`False`; for `+inf`, `inf < 0` is `False`. So:
```
CostAssumptions(float('nan'), 0.1, 0.2).validate()  -> passes
CostAssumptions(inf, 0.1, 0.2).validate()           -> passes
cost_tiers(NaN assumptions) -> floor=nan, conservative=nan
```
A NaN/inf cost tier would silently propagate NaN into `cost_r` and
`net_expectancy_r`, i.e. an unvalidated value reaching a decision-grade number.
This is the same class the T14 work guards at the feature layer. It is
**non-blocking** here because costs are developer-set constants, not
market-derived data — but the fix is one line: `if not math.isfinite(value) or
value < 0.0: raise`. Recommend adding it for consistency with the T14 standard.

---

## Result

**PASS.** RULE B is satisfied: three cost tiers, honest ordering, correct
reference-only flag, correct R-math, and a clean deduplication of the T15 levels
module. Recommend the Lead rule on the canonical path (`src/models/costs.py` vs
`src/costs/`); the audit does not depend on that choice.

## Notes

- **E04.** With T20 passing audit, E04 (RULE B cost tiers) can be marked RESOLVED
  — the tiers now exist and are decision-grade. Recommend closing it.
- **Assumptions are stated, not measured.** The 0.30/0.10/0.20 are the mission's
  numbers, not measured from a live feed; re-derive when real data lands (E05).
  Correctly disclosed.
- **No fabrication.** Probe in `/tmp`, uncommitted; reproduces from `0d9e64b`.
- **Independence.** Agent-D authored none of the audited code.

---

# ADDENDUM A — re-audit after F20-1 fix

- **Date:** 2026-10-07 23:11 UTC
- **Repo HEAD at re-audit:** 5b61905
- **Verdict:** **PASS (unchanged); F20-1 FIXED.**

`CostAssumptions.validate()` now rejects non-finite values:
`if not math.isfinite(value) or value < 0.0: raise`. Verified independently —
`NaN`, `+inf`, `-inf` are all rejected; `cost_tiers(CostAssumptions(0.1, nan,
0.2))` raises. A regression test (`test_nan_and_inf_rejected`) covers all three
plus the tier path. 226 tests pass. No behavioural change for finite costs (tier
values identical). T20 remains PASS; E04 may be closed.
