# Agent-B — Probability & Calibration harness

Owned by Agent-B. Reviewer: Agent-D.

This directory is the model + calibration **harness only**. It contains no
fitted model and emits no probability: per RULE C, nothing here may publish a
probability until calibration is measured and audited (Agent-D T11).

## Modules

| File | Purpose |
|---|---|
| `splits.py` | Chronological development / validation / OOS split (2021-22 / 2023-24 / 2025). Disjoint, ordered, no shuffling. |
| `walk_forward.py` | Rolling windows, fixed train/test size, deterministic fold count. |
| `calibration.py` | Brier, Brier skill, ECE, MCE, reliability diagram, coverage per tier. |
| `api_contract.py` | DRAFT probability output contract for later handoff to Agent-C. Not published. |
| `features.py` | Validated adapter for Agent-A's feature vectors (mirrors `AnalyticalFeatures.h`). No recomputation. |
| `dataset.py` | Causal labeling + purged chronological split (label-window embargo at seams). |
| `logistic.py` | Deterministic logistic regression (IRLS/Newton, ridge). No sklearn/numpy. |
| `baseline.py` | T03 runner: fit on development, measure dev/validation, OOS only on request. |
| `demo_baseline.py` | Deterministic end-to-end demo on SYNTHETIC data (pipeline check, not a result). |
| `calibrators.py` | T05 fitting: Platt, isotonic (PAVA), histogram. Turn raw scores into calibrated probabilities. |
| `calibrated.py` | T05 runner: fit base on dev, fit calibrator on val, evaluate OOS. Leakage-separated by construction. `model_factory` selects the base estimator. |
| `demo_calibrated.py` | Deterministic raw-vs-calibrated demo on SYNTHETIC data. |
| `gbt.py` | T04: deterministic logistic-loss gradient-boosted trees (stdlib, XGBoost-style). No xgboost/numpy. |
| `demo_gbt.py` | Deterministic logistic-vs-GBT comparison through the calibrated runner. |
| `levels.py` | T15: cost tiers (RULE B), ATR, SL/TP, risk tiers, hit statistics (RULE A). |
| `horizon.py` | T15: cost-aware UP/DOWN/FLAT labels and per-horizon calibration comparison. |
| `demo_levels.py` | Deterministic T15 validation demo (synthetic): Q-horizon + cost-tier expectancy. |

## Design constraints

- Python standard library only. No numpy / sklearn / xgboost at this layer, so
  the harness is deterministic and runs on a bare container.
- Every function is a pure function of its inputs.
- Every metric has an explicit success/failure threshold from `MISSION.md`
  (`ECE < 0.05`, Brier better than the 0.25 baseline, `ECE > 0.10` is failure).

## Running the tests

From the repository root:

```bash
python3 -m unittest discover -s tests/models -t . -v
```

212 deterministic tests, no third-party dependencies. Includes a parity check
against real `AnalyticalFeatureEngine` output (`tests/models/fixtures/engine_set.json`).

## T04 status

Stdlib gradient-boosted trees (`gbt.py`), composed with the T05 calibrators via
`run_calibrated(..., model_factory=...)`. No third-party dependency was added, so
the harness stays hermetic. Uncalibrated/unpublished until the T11 audit (RULE C).

```bash
python3 -m src.models.demo_gbt
```

## T05 status

Calibrators (Platt / isotonic / histogram) and a leakage-separated calibrated
runner. Fit-on-validation, evaluate-on-OOS is enforced structurally. This is the
mission's deliverable shape, but the numbers remain uncalibrated-and-unpublished
until the T11 audit (RULE C).

```bash
python3 -m src.models.demo_calibrated
```

## T03 status

Logistic baseline implemented and passing its tests on synthetic data. It is a
RESEARCH estimator: uncalibrated and unpublished (RULE C), no cost tiers yet
(RULE B), no real XAUUSD data available in the container. OOS is not touched
during development.

Run the synthetic pipeline check:

```bash
python3 -m src.models.demo_baseline
```
