# Agent-B - Important Info
> Living document. Updated, not appended.

## Role
Probability & Calibration. Owns the logistic baseline, XGBoost + calibration, and calibration metrics. Calibration must precede any probability output (RULE C).

## Owned files
- src/models/
- tests/models/
- coordination/agent-b/

## Forbidden files
- The baseline (base9 / baseold) - READ-ONLY.
- src/foundation/ (frozen contracts), src/analysis/features/, bridge/, src/api/, packaging/.
- coordination/<other-agent>/ folders.

## Features / deliverables
- Model harness skeleton (Phase 2.0), stdlib-only Python:
  - `src/models/splits.py` — chronological dev(2021-22)/val(2023-24)/OOS(2025), disjoint + causal guard
  - `src/models/walk_forward.py` — deterministic rolling windows, fixed train/test, leakage guard
  - `src/models/calibration.py` — Brier, Brier skill, ECE, MCE, reliability diagram, coverage tiers
  - `src/models/api_contract.py` — DRAFT probability contract (NOT published, RULE C)
  - `src/models/features.py` — validated adapter for Agent-A's feature vectors (mirrors AnalyticalFeatures.h),
    enforces one common decision instant across all 9 streams (regression guard for Agent-D T01 F1/F2)
  - `src/models/dataset.py` — causal labeling + purged chronological split (seam embargo)
  - `src/models/logistic.py` — deterministic IRLS logistic regression (ridge)
  - `src/models/baseline.py` — T03 runner (dev-fit, OOS gated)
  - `src/models/demo_baseline.py` — deterministic synthetic pipeline check
  - `src/models/calibrators.py` — Platt / isotonic (PAVA) / histogram calibrators
  - `src/models/calibrated.py` — leakage-separated calibrated runner (dev-fit, val-cal, OOS-eval)
  - `src/models/demo_calibrated.py` — deterministic raw-vs-calibrated synthetic check
  - `src/models/gbt.py` — T04 stdlib deterministic gradient-boosted trees
  - `src/models/calibrated.py` — now accepts `model_factory` (logistic or GBT)
  - `src/models/demo_gbt.py` — deterministic logistic-vs-GBT calibrated comparison
  - `src/models/levels.py` — T15 cost tiers, ATR, SL/TP, risk tiers, hit stats
  - `src/models/horizon.py` — T15 cost-aware labels + per-horizon calibration
  - `src/models/demo_levels.py` — T15 synthetic validation demo
  - `src/models/dataset.py::assert_partitions_separated` — structural overlap/order guard
  - `tests/models/*` — 212 deterministic tests, all passing
  - `tests/models/fixtures/engine_set.json` — real C++ engine output captured for parity (commit 60d04cb)

## Key findings
- No third-party Python packages exist in the container (no numpy/sklearn/xgboost/pytest).
  Harness is stdlib-only by necessity and for determinism.
- The mission's success metrics (ECE<0.05, Brier<0.25) now have real code behind them
  (`calibration.py`); previously they existed only as prose.
- Nothing calibrated or published yet. T03 NOT claimed — waiting on T01 features.

## Open questions
- Where does the 3-month dataset come from? (blocked on Lead Q2)
- What exactly are base9 / baseold? (blocked on Lead Q3) — needed for T12 control comparison.
- Which language/format will Agent-A's features be delivered in? Harness currently
  assumes a `Sample(timestamp, payload)` row shape and can adapt.

## Useful commands
- Run harness tests: `python3 -m unittest discover -s tests/models -t . -v`
