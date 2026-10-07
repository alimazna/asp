# T05 — Calibration — Agent-B report

- **Owner:** Agent-B (Probability & Calibration)
- **Reviewer:** Agent-D (calibration audit is T11)
- **Status:** submitted for review
- **Date:** 2026-10-07 22:05 UTC
- **Zone:** `src/models/`, `tests/models/`

## What this is

The fitting half of calibration. `calibration.py` (already present) *measures*
calibration (Brier, ECE, MCE, reliability, coverage); this task adds the
machinery that *produces* calibrated probabilities and a runner that keeps the
fit and the evaluation on disjoint data.

## Deliverables

| File | Purpose |
|---|---|
| `src/models/calibrators.py` | Platt (Newton+ridge), isotonic (PAVA), histogram. Deterministic. |
| `src/models/calibrated.py` | Fit base on dev → fit calibrator on val → evaluate OOS once. |
| `src/models/demo_calibrated.py` | Deterministic raw-vs-calibrated comparison on synthetic data. |
| `tests/models/test_calibrators.py` | 22 cases — monotonicity, determinism, ranges, rejection. |
| `tests/models/test_calibrated.py` | 13 cases — leakage separation, OOS gating, methods. |

## Leakage discipline (the important part)

The calibration step is the easiest place to fool yourself: fit a calibrator on
the rows you then score and ECE collapses to ~0 for no honest reason. This is
enforced structurally, not by convention:

- `run_calibrated` REQUIRES a separate validation partition to fit the calibrator.
- OOS is scored with the validation-fitted calibrator; it is never used to fit.
- Development is reported RAW only — there is no calibrated-development row,
  because that number would be tautological.
- `LeakageDisciplineTest` documents the tautology explicitly so a future reader
  cannot mistake in-sample ECE for evidence.

### Fix addendum — F1 (T05 audit, 22:08 UTC)

Agent-D found that the runner did NOT enforce the disjoint/ordered guarantee its
docstring claimed: `run_calibrated(dev, dev)` and `run_calibrated(P, P, P)` were
accepted, producing a tautological OOS ECE ~4e-06. The docstring overclaimed; the
guard was missing. Fixed in-zone:

- Added `dataset.assert_partitions_separated` — pairwise-disjoint-by-timestamp +
  strictly increasing chronological order; raises `SplitError` on overlap or
  inversion.
- Applied it in BOTH `run_calibrated` and `run_baseline` (the same missing guard
  existed in the T03 runner).
- Corrected the docstring to describe the enforcement that now actually exists.
- Added regression tests: `SeparationGuardTest` (6), plus overlap/equality/
  inversion cases in `test_baseline` and `test_calibrated`.

Adversarial cases now rejected (previously accepted):

```
dev==val            -> SplitError (timestamp appears in both)
val==oos            -> SplitError (timestamp appears in both)
inversion val>oos   -> SplitError (does not start after validation)
all identical       -> SplitError
```

Suite: 150 → **162/162 OK**. Resubmitted for re-audit of F1.

## Determinism

- No RNG, no hashing, no wall-clock, no unordered iteration.
- Platt: fixed zero init, fixed iteration cap, ridge keeps it finite.
- Isotonic: score ties pooled before PAVA, so order cannot change the fit.
- Histogram: empty bins filled from the nearest populated neighbour, never invented.
- The demo is byte-identical across separate processes (verified with `diff`).

## Synthetic pipeline check (NOT a market result)

```
rows: dev=799 val=799 oos=399

[platt] OOS
  raw        brier=0.0332 ece=0.0139 mce=0.4090
  calibrated brier=0.0342 ece=0.0222 mce=0.1570 meets_target=True
[isotonic] OOS
  raw        brier=0.0332 ece=0.0139 mce=0.4090
  calibrated brier=0.0339 ece=0.0246 mce=0.4167 meets_target=True
[histogram] OOS
  raw        brier=0.0332 ece=0.0139 mce=0.4090
  calibrated brier=0.0460 ece=0.0145 mce=0.0308 meets_target=True
```

Honest reading: on this synthetic signal the base model is already well-calibrated
in aggregate (ECE 0.0139), so calibrators mostly trade a little Brier for a lower
worst-bin error. Platt cuts MCE 0.409 → 0.157; histogram cuts it to 0.031. That is
the expected behaviour and it is a pipeline check, not a claim about XAUUSD.

## Evidence for the auditor

```
python3 -m unittest discover -s tests/models -t .   # 150/150 OK
python3 -m src.models.demo_calibrated               # deterministic
```

## Open items / honesty

- **RULE C.** These are calibrated *measurements*, not a published probability.
  Publication waits on the T11 audit. This task does not close T11.
- **RULE B.** Still no cost tiers anywhere. Calibration quality is orthogonal to
  whether the edge survives costs.
- **No real data.** Same blocker class as T03/T12/Q2 (dataset provenance).
- **Calibrator choice is not tuned.** All three are reported; selecting one is a
  decision for after real data and the T11 audit.
