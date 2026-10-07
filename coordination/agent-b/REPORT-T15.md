# REPORT — T15 (Decision model validation: horizon + SL/TP)

- **Agent:** agent-b (Probability & Calibration) — co-owner with the Lead
- **Zone:** `src/models/`, `tests/models/`
- **Status:** submitted for REVIEW
- **Date:** 2026-10-07
- **Head:** (this commit)

## What was built (in-zone)

| File | Change |
| --- | --- |
| `src/models/levels.py` | NEW. Cost tiers (RULE B), ATR proxy, SL/TP (`atr_1.5x` / `rr_2x`), risk tiers, hit simulation, per-tier expectancy. |
| `src/models/horizon.py` | NEW. Cost-aware UP/DOWN/FLAT labels; per-horizon calibration comparison with the T05 structural guard. |
| `src/models/demo_levels.py` | NEW. Deterministic synthetic validation of Q-horizon and the cost-tier SL/TP study. |
| `tests/models/test_levels.py` | NEW. 21 cases. |
| `tests/models/test_horizon.py` | NEW. 13 cases. |

## Answers to the DECISION_MODEL open questions

**Q-horizon** (synthetic, conservative dead-band theta=0.60):

| H (bars) | FLAT share | n(OOS) | Brier | ECE | label |
| --- | --- | --- | --- | --- | --- |
| 1 | 0.83 | 107 | 0.0000 | 0.0000 | probability |
| 4 | 0.25 | 454 | 0.0264 | 0.0259 | probability |
| 16 | 0.04 | 562 | 0.1279 | 0.0421 | probability |

**The H=1 "perfect" calibration is an artifact, not skill.** The synthetic
generator drives price and features from the same latent state, so a 1-bar label
is trivially predictable. I am recording it as a **red flag on the generator**,
not as evidence. The usable synthetic signal is H=4, where the pipeline still
calibrates (ECE 0.026). **No horizon recommendation can be made until real data
exists** — on synthetic data the ranking is not informative.

**Q-theta:** the dead-band works and is cost-anchored (theta = conservative
round-trip = 0.60). FLAT share falls from 0.83 (H=1) to 0.04 (H=16), as expected.
Whether a volatility-scaled dead-band is needed is **unresolved** — it needs real
data to answer.

**SL/TP + RULE A:** canonical TP stays `rr_2x`; `prob_scaled` is not implemented
as canonical (it is the RULE A mechanism). Expectancy is computed in R units and
charged per cost tier:

```
zero          mean_net=+1.500R   [reference only]
floor         mean_net=+0.950R   [decision-grade]
conservative  mean_net=+0.674R   [decision-grade]
```

(These gross numbers are inflated by the synthetic generator's predictability and
must not be read as market expectancy.)

## RULE compliance

- **RULE A:** RR fixed at 2.0; probability never shrinks the target.
- **RULE B:** every decision number reported under the three cost tiers; tier 1 is
  flagged reference-only and non-decision-grade.
- **RULE C:** `HorizonResult.label` is `"score"` whenever ECE >= 0.05, else
  `"probability"` — the gate is in code, not prose.
- **RULE D:** coverage tiers reused from `calibration.py`.
- **RULE E:** reports are immutable; negative/artifact results recorded, not hidden.

## Verification

```
python3 -m unittest discover -s tests/models -t .
Ran 212 tests ... OK
```

Demo byte-identical across processes. Synthetic only — no market claim.

## Residual / not claimed

- **No real data** — everything here is synthetic; Q-horizon/Q-theta are
  unresolved for the real market.
- **ATR is a close-to-close proxy** (no high/low at this layer). Documented.
- **No walk-forward beyond the fixed year split** — a rolling walk-forward is a
  later refinement.

## Ask

- Agent-D: audit T15 — the label boundary (move == theta is FLAT), the cost
  charging per tier, the RULE C in-code gate, and the H=1 artifact caveat.
- Lead: confirm the DECISION_MODEL.md text should be updated with the
  "H=1 synthetic artifact" caveat, or leave the doc as design-only.
