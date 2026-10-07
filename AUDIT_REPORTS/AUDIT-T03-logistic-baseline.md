# Audit Report — T03 (Logistic baseline)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-B (Probability & Calibration)
- **Date:** 2026-10-07 21:58 UTC
- **Repo HEAD at audit:** 13f7694 (T03 commit); audited at bf4e3aa HEAD
- **Verdict:** **PASS — with two recorded scope limits (no real data, RULE B unbuilt).**
  Causality (purge/embargo), determinism, and OOS gating are correct and
  independently reproduced. T03 may go to **DONE** at the Lead's confirmation.
  The scope limits are honestly disclosed by Agent-B and are not defects.

---

## Claim

Agent-B, `coordination/agent-b/comm.md` 21:55 UTC, "T03 submitted for review"
(`Reply required: yes`):

> Delivered (zone `src/models/`, `tests/models/`): dataset.py (causal labeling +
> PURGED chronological split), logistic.py (deterministic IRLS/Newton with
> ridge), baseline.py (fit dev, measure dev+val; OOS only if caller passes it),
> demo_baseline.py, tests (14+20+6). Suite 119/119 OK.
> Honesty: RULE C — probabilities UNcalibrated/UNpublished; RULE B — no cost
> tiers; NO REAL DATA — synthetic generator only; determinism byte-identical.

Commit under audit: `13f7694` "agent-b: T03 logistic baseline
(dataset/logistic/baseline) + tests (119 pass)".

Acceptance: logistic baseline model; calibration **before** any probability
output (RULE C); no leakage.

---

## Evidence inspected

- **Commit:** `13f7694`
- **Files:** `src/models/{dataset,logistic,baseline,demo_baseline}.py`,
  `tests/models/{test_dataset,test_logistic,test_baseline}.py`,
  `coordination/agent-b/REPORT-T03.md`.
- **Agent-D reran everything** and wrote an independent invariant probe.

```
$ python3 -m unittest discover -s tests/models -t .   -> 119/119 OK
$ python3 -m src.models.demo_baseline > a ; ... > b ; diff a b   -> identical
$ PYTHONPATH=. python3 /tmp/t03_probe.py   (Agent-D probe)
  counts dev/val/oos: 799 799 399
  seam dev->val: boundary=1675411200 head_rows_crossing=0
  seam val->oos: boundary=1741449600 head_rows_crossing=0
  PURGE INVARIANT HOLDS: True
  label_timestamp > timestamp for all rows: True
  OOS untouched by default (oos is None): True
  OOS evaluated only when passed: True
  column pinning enforced (hard error): partition 'validation' has different...
  fit deterministic (weights identical): True
```

---

## Verification steps

1. **Reran the suite.** 119/119 pass.
2. **Purge/embargo invariant (causality).** Independent probe confirms **no**
   development row has `label_timestamp >= validation[0].timestamp`, and no
   validation row crosses into OOS. The label window of every training row stays
   inside its partition. Correct.
3. **Label causality.** Every `LabeledExample.label_timestamp > timestamp`
   (`validate()` enforces it); the last `horizon` snapshots are dropped, so no
   row is labeled by peeking past the data. Correct.
4. **OOS gating.** `run_baseline` returns `oos=None` unless OOS is passed
   explicitly; verified both branches. OOS is evaluated once, only on request.
5. **Column pinning.** Columns are fixed from development; a validation partition
   with a different column set raises `SplitError` (verified). Val/OOS cannot
   influence the feature set.
6. **Standardizer leakage.** The `Standardizer` is fit inside `fit_logistic` on
   the **development** matrix only; validation/OOS are transformed with the
   dev-fitted mean/std. No scaling leakage.
7. **Determinism.** Demo output is byte-identical across separate processes
   (`diff` clean); the fit is deterministic (identical weights/intercept across
   runs); no RNG, hashing, wall-clock, or unordered iteration in the model layer.
8. **Numerical robustness.** `_solve` raises on a singular system instead of
   returning NaN; `sigmoid` is overflow-safe; ridge keeps near-collinear systems
   solvable. Sound.
9. **RULE C honesty.** `baseline.py`/`logistic.py` state the probability is
   uncalibrated and unpublished; no calibration step is wired in. Correct.
10. **RULE B honesty.** No cost-tier model exists; explicitly flagged, not
    silently omitted. Correct.
11. **Synthetic-data honesty.** The demo prints a SYNTHETIC banner and states it
    is a pipeline check, not a market result. The generator injects the latent
    signal directly, so the high accuracy is tautological — Agent-B says so
    plainly. Correct and conservative.
12. **Zone.** `13f7694` touched `src/models/` and `tests/models/` — in-zone; no
    production/frozen edits.

---

## Result

**PASS.** The T03 logistic baseline is causal (purged seams, causal labels, dev-only
scaling), deterministic, and correctly gates OOS. RULE C is respected (nothing
published). T03 may go to **DONE** at the Lead's confirmation.

## Notes

- **Two scope limits (recorded, not defects).**
  1. **No real data.** All numbers are synthetic. T03 is *correct* but not
     *evidential*. This is the same blocker class as T12/Q2 (dataset provenance).
  2. **RULE B unbuilt.** No cost tiers anywhere; cost-aware evaluation is
     downstream and absent. Flagged by Agent-B and by the Lead's T12 escalation.
- **The reported accuracy (0.9462) carries no market information.** The synthetic
  generator injects the latent relationship, so the pipeline merely recovers what
  it injected. Not a claim about XAUUSD; Agent-B labels it as such.
- **No fabrication by the auditor.** All findings reproduce from `13f7694`; the
  probe is in `/tmp` and intentionally uncommitted.
- **Independence.** Agent-D authored none of the audited code.
- **Calibration is NOT audited here.** The Brier/ECE numbers in the T03 report are
  measurement output; the formal calibration audit is **T11**, still IDLE (no
  calibrated, published probability exists yet). T03 does not close T11.
