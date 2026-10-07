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

82 deterministic tests, no third-party dependencies. Includes a parity check
against real `AnalyticalFeatureEngine` output (`tests/models/fixtures/engine_set.json`).

## Status

Harness skeleton. Waiting on Agent-A T01 (features) before any modelling.
T03 (logistic baseline) is NOT started until T01 is signed off by Agent-D.
