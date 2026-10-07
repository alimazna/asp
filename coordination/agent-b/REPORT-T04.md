# REPORT — T04 (Boosted model + calibration composition)

- **Agent:** agent-b (Probability & Calibration)
- **Zone:** `src/models/`, `tests/models/`
- **Status:** submitted for REVIEW
- **Date:** 2026-10-07
- **Head:** (this commit)

## Decision taken

The Lead delegated the T04 model choice. I took the **stdlib deterministic
booster** option and did **not** install `xgboost`/`numpy`:

- the container has no third-party Python packages and the T03/T05 harness is
  stdlib-only and byte-reproducible; adding an unpinned wheel would break
  hermeticity and add supply-chain surface;
- a packaging/dependency task is separately in scope for the packaging zone
  (T07-class), so T04 should not create an untracked dependency;
- determinism is a hard requirement here (RULE E / audit reproducibility), and
  a from-scratch GBT is deterministic by construction.

If the Lead prefers a real-XGBoost parity check later, it should be a separate
pinned task, not a silent install inside T04.

## What was built (in-zone)

| File | Change |
| --- | --- |
| `src/models/gbt.py` | NEW. Logistic-loss gradient-boosted trees (XGBoost-style second-order boosting), pure stdlib. |
| `src/models/calibrated.py` | `run_calibrated` gained `model_factory` so any estimator (logistic or GBT) composes with the audited calibration runner. |
| `src/models/demo_gbt.py` | NEW. Deterministic logistic-vs-GBT comparison through the calibrated runner. |
| `tests/models/test_gbt.py` | NEW. 15 cases. |
| `tests/models/test_calibrated.py` | +1 case: GBT factory path through `run_calibrated`. |

## Design (determinism)

- Split thresholds are midpoints of adjacent **distinct** feature values from a
  sorted list — no quantile sketch, no sampling.
- Candidate thresholds are scanned in ascending order; ties break by lowest
  feature index, then best gain.
- No RNG, no hashing, no wall-clock, no unordered iteration.
- Scores are maintained incrementally per boosting round (O(n) per round).

## Leakage / RULE compliance

- **RULE C:** GBT outputs are uncalibrated research probabilities; nothing is
  published. The demo routes through the same leakage-separated calibrator as
  T05 and the T11 gate still applies.
- **RULE B:** no cost tiers here — probability quality only.
- **RULE E:** reports are immutable; nothing overwritten.
- The calibrated runner still calls `assert_partitions_separated` before fitting,
  so GBT + calibration inherits the T05 F1 guard unchanged.

## Bug found and fixed during T04 (worth the auditor's attention)

First implementation flattened sub-trees without rebasing their **relative** child
indices, so an internal node could point at itself and `predict()` looped forever.
Symptom: `fit_gbt(..., max_depth=3)` never returned (silent hang, no error).
Fix: `_rebased()` shifts child indices by each sub-tree's offset in the combined
array. Regression tests added:
`test_deep_tree_predict_terminates`, `test_internal_nodes_never_self_reference`.

This is exactly the class of defect that only shows at depth > 1, so shallow
smoke tests would have missed it — the auditor should confirm at `max_depth>=3`.

## Verification

```
python3 -m unittest discover -s tests/models -t .
Ran 178 tests ... OK
```

- Demo is byte-identical across processes (verified by diff).
- Synthetic data only — no market claim.

## Residual / not claimed

- **No real data** — all numbers synthetic (T03/T12 class). Not evidence about XAUUSD.
- **Calibrator choice not tuned** — all three reported; selection is post-T11.
- **No XGBoost parity** — a real-booster cross-check is a separate pinned task if wanted.

## Ask

- Agent-D: audit T04 on this head — verify the deep-tree regression, the
  `model_factory` path, and that the partition guard is still enforced for GBT.
- Lead: T05 is re-audited PASS (Agent-D, `6e8bd15`); request DONE confirmation and
  the T11 go/no-go.
