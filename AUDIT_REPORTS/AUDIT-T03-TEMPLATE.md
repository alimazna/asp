# Audit Report — T03 (Logistic baseline) — CALIBRATION

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-B (Probability & Calibration)
- **Date:** <fill on audit>
- **Repo HEAD at audit:** <fill>
- **Verdict:** PASS | FAIL | NEEDS WORK

---

## Claim

<What Agent-B claimed. Quote their "ready for audit" comm.md entry verbatim,
with timestamp and commit hash. State the acceptance criteria: logistic
baseline model; calibration **before** any probability output (RULE C).>

---

## Evidence inspected

- **Commit(s):** <hash(es) claimed>
- **Files:** <source/test files>
- **Test output:** <exact command(s) run and raw output — Agent-D reruns them>
- **Raw commands + output:** <paste below>

```
<paste exact commands and output>
```

---

## Verification steps

Calibration audit is adversarial: a model is uncalibrated until measured.

1. **Separation of score/confidence/probability.** Confirm the output is a
   calibrated probability, not a score relabelled (V4-14: SCORE != PROBABILITY).
2. **Calibration method present.** Identify the method (Platt / isotonic / binning)
   and confirm it is fitted on data disjoint from the evaluation set.
3. **ECE measured.** Reproduce the expected calibration error computation.
   Check the binning, sample sizes per bin, and that empty bins are handled.
4. **Brier measured** against the 0.25 uninformative baseline. Confirm the
   baseline is computed on the same sample, not assumed.
5. **Coverage honesty (RULE D).** Confirm coverage is reported explicitly and
   a high-confidence subset is not presented as the whole sample.
6. **No lookahead in calibration fit.** The calibrator must be fit only on
   in-sample/train data; the evaluation must be out-of-sample.
7. **Reproduce independently.** Rerun Agent-B's tests/notebook from clean.
8. **Cost tiers (RULE B).** Confirm results are reported under the three cost
   tiers (or that absence of a cost-tier model is explicitly flagged, not
   silently omitted).

---

## Result

PASS | FAIL | NEEDS WORK

<One-line justification. If FAIL/NEEDS WORK, list exact failures with the
command and observed numbers that demonstrate each.>

---

## Notes

<Residual uncertainty; whether the numbers are reproducible from the repo alone;
whether the dataset is real MT5 data or synthetic; any ECE/Brier figure that is
claimed but not reproducible.>
