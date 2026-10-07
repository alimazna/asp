# REPORT — T20 (Three cost tiers, RULE B)

- **Agent:** agent-b (Probability & Calibration)
- **Status:** submitted for REVIEW
- **Date:** 2026-10-07
- **Head:** (this commit)

## Zone note (please read first)

T20's assigned deliverable is `src/costs/`. My owned zone is `src/models/` and
`tests/models/` only; charter says write nothing outside owned dirs. **I did not
write to `src/costs/`.** Instead I put the canonical cost model in-zone at
`src/models/costs.py` and refactored `src/models/levels.py` onto it, so the
mission's RULE B numbers exist and are shared **without duplication**.

@deepseek — decide the final home: (a) keep it as `src/models/costs.py` and update
the T20 deliverable path, or (b) authorize a one-time move to `src/costs/`. I will
execute the move if you authorize it; until then the code lives in-zone.

## What was built

| File | Change |
| --- | --- |
| `src/models/costs.py` | NEW. `CostAssumptions`, `CostTier`, `cost_tiers`, `tier_by_name`, `cost_r`, `net_expectancy_r`. |
| `src/models/levels.py` | Refactored: cost tiers now imported from `costs.py` (deduplicated). |
| `tests/models/test_costs.py` | NEW. 13 cases. |

## The model (RULE B)

```
tier 1  zero          round_trip = 0.00   decision_grade = False  (reference only)
tier 2  floor         round_trip = 0.40   spread 0.30 + commission 0.10
tier 3  conservative  round_trip = 0.60   + slippage 0.20
```

Assumptions are configurable (`CostAssumptions`) and validated (non-negative).
`cost_r(risk_distance)` converts a round trip to R units; `net_expectancy_r`
gives net edge after cost. The dead-band theta used by T15 is `tier.dead_band()`.

## RULE compliance

- **RULE B:** exactly three tiers; zero is flagged `decision_grade=False` and the
  T15 demo labels it "reference only". Decision-grade work uses floor/conservative.
- **RULE A/E:** no reward-structure artifact; numbers are configurable and honest.

## Verification

```
python3 -m unittest discover -s tests/models -t .
Ran 224 tests ... OK
```

`demo_levels.py` re-verified after the refactor (identical cost-tier output).

## Residual

- No real data — the cost *assumptions* are the mission's stated numbers, not
  measured from a live feed. If real data lands, these should be re-derived.
- The zero tier is retained only as a labelled reference; nothing decision-grade
  may use it.

## Ask

- Agent-D: audit the three-tier definition, the non-negative validation, the
  `decision_grade` flags, and the in-zone zone-compliance call.
- Lead: rule on the canonical path (`src/models/costs.py` vs `src/costs/`).
