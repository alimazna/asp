# T03 — Logistic Baseline — Agent-B report

- **Owner:** Agent-B (Probability & Calibration)
- **Reviewer:** Agent-D
- **Status:** submitted for review
- **Date:** 2026-10-07 21:55 UTC
- **Zone:** `src/models/`, `tests/models/`

## What this is

A deterministic, dependency-free logistic-regression baseline for the calibrated
probability system, plus the causal dataset/purge machinery needed to fit it
without leakage. It fits on the development partition and measures on development
and validation. OOS is only evaluated when explicitly supplied.

## What this is NOT

- **Not a published probability (RULE C).** The probabilities are uncalibrated and
  unpublished. Calibration is measured, not established; the T11 audit has not run.
- **Not a market result.** No real XAUUSD data exists in the container. All numbers
  below are from a SYNTHETIC, deterministic generator.
- **Not cost-aware (RULE B).** T03 is probability-quality only; no trade simulation,
  no cost tiers. Cost-aware evaluation is downstream (needs RULE B, still unbuilt).

## Deliverables

| File | Purpose |
|---|---|
| `src/models/dataset.py` | Causal labeling + purged chronological split (seam embargo). |
| `src/models/logistic.py` | IRLS/Newton logistic regression, ridge, stable Gauss-Jordan solve. |
| `src/models/baseline.py` | T03 runner: fit on dev, measure dev/val, OOS on request. |
| `src/models/demo_baseline.py` | Deterministic end-to-end demo on synthetic data. |
| `tests/models/test_dataset.py` | 14 cases — labeling, purge, columns. |
| `tests/models/test_logistic.py` | 20 cases — sigmoid, standardizer, fit, determinism. |
| `tests/models/test_baseline.py` | 6 cases — runner, OOS gating, column pinning. |

## Causality / leakage controls

1. **Label embargo at seams.** A development row whose forward label window would
   reach into validation is purged (`dataset.purge_split`); likewise validation vs
   OOS. No training row is scored by the next partition's information.
2. **Columns pinned to development.** Validation/OOS cannot influence the feature
   set; a mismatch is a hard error (`baseline.run_baseline`).
3. **OOS untouched by default.** `run_baseline` returns `oos=None` unless the caller
   passes an OOS partition explicitly.
4. **Input causality** is inherited from Agent-A's engine (T01 PASS, T10 PASS) and
   enforced by `features.py` (one common decision instant).

## Determinism

- No RNG, no hashing, no wall-clock, no unordered iteration.
- Fixed zero initialisation, fixed iteration cap, stable linear solve.
- The demo is byte-identical across separate processes (verified with `diff`).

## Synthetic pipeline check (NOT a market result)

```
SYNTHETIC DEMO — not a market result (no real data available)
rows: total=1999 dev=799 val=799 oos=399
model: converged=True iters=56 features=20
development: n=799 acc=0.9462 brier=0.0340 skill=+0.8639 ece=0.0230 meets_target=True
validation:  n=799 acc=0.9449 brier=0.0408 skill=+0.8368 ece=0.0237 meets_target=True
```

The generator injects a known latent relationship, so a high score is expected and
proves only that the pipeline works. It says nothing about XAUUSD.

## Evidence for the auditor

```
python3 -m unittest discover -s tests/models -t . -v   # 119/119 OK
python3 -m src.models.demo_baseline                     # deterministic
```

## Open items / honesty

- **No real data.** T03 cannot produce a market claim until a dataset is supplied.
  Where does the 3-month XAUUSD history come from? (Lead Q2, still open.)
- **No calibration step yet.** That is T04/T05; T11 audits it.
- **No cost tiers (RULE B).** Not implemented anywhere in the repo.
- **Threshold 0.5.** Accuracy uses a fixed 0.5 threshold; threshold selection is a
  calibration decision and is deliberately not tuned here.
