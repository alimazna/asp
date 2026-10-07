# Audit Report — T11 (Calibration audit) — CALIBRATION

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-D (self); subject is Agent-B's calibration surface
  (`src/models/calibration.py`, `src/probability/ProbabilityEngine.*`) and any
  model that emits a probability.
- **Reviewer of this audit:** Lead (DeepSeek)
- **Date:** <fill on audit>
- **Repo HEAD at audit:** <fill>
- **Verdict:** PASS | FAIL | NEEDS WORK

This template is pre-written (T11 is IDLE). RULE C: no probability may be
published until T11 passes. Fill every `<...>` from evidence, never from a claim.

---

## Claim

<What is being audited and by whom. Quote the owner's "ready for audit" comm.md
entry verbatim (timestamp + commit hash). State the acceptance criteria:
calibration measured and reported before any probability output; ECE target
< 0.05, failure > 0.10; Brier better than the 0.25 uninformative baseline;
coverage honesty (RULE D); results under the three cost tiers (RULE B).>

---

## Evidence inspected

- **Commit(s):** <hash(es)>
- **Files:** `src/models/calibration.py`, `src/probability/ProbabilityEngine.{h,cpp}`,
  model code that produces probabilities, and their tests.
- **Raw commands + output:** <paste exact commands and output below>

```
<paste exact commands and output — Agent-D reruns them>
```

---

## Verification steps

Calibration audit is adversarial: a model is uncalibrated until measured. Default
assumption — the reported numbers are unreproducible and the sample is not what
it claims — until each step is reproduced.

1. **Score vs probability (V4-14).** Confirm the output is a calibrated
   probability, not a score relabelled. SCORE != PROBABILITY.
2. **Calibration method present and named.** Identify the method (Platt /
   isotonic / binning) and confirm it is fit on data disjoint from evaluation.
3. **ECE reproduced.** Rerun the ECE computation. Check binning, per-bin sample
   sizes, and empty-bin handling. Compare to `ECE_TARGET` / `ECE_FAILURE`.
4. **Brier reproduced** against the 0.25 baseline, computed on the SAME sample
   (not assumed). Report Brier skill score.
5. **Coverage honesty (RULE D).** Confirm coverage is reported explicitly and a
   high-confidence subset is not passed off as the whole sample.
6. **No lookahead in the calibration fit.** Fit only on train; evaluate
   out-of-sample. Re-run with the split boundary moved to confirm.
7. **Cost tiers (RULE B).** Results reported under the three cost tiers, or the
   absence of a cost-tier model explicitly flagged — not silently omitted.
8. **Independent reproduction.** Rerun the owner's tests from clean; do not
   trust the claimed counts.
9. **Honesty / vocabulary (MISSION §8).** Confirm a probability is described as a
   claim about frequency, and any UNKNOWN/UNCALIBRATED state is not presented as
   a usable probability.

---

## Result

PASS | FAIL | NEEDS WORK

<One-line justification. If FAIL/NEEDS WORK, list each failure with the exact
command and the observed numbers that demonstrate it.>

---

## Notes

<Residual uncertainty; whether the numbers are reproducible from the repo alone;
real MT5 data vs synthetic; any ECE/Brier figure claimed but not reproducible;
whether the calibrator exists at all (if not, T11 is BLOCKED, not FAIL).>
