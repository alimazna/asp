# Audit Report — T11 (Calibration audit)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-D (self); subject is Agent-B's calibration surface
  (`src/models/calibration.py`, `src/models/calibrators.py`,
  `src/models/calibrated.py`).
- **Reviewer of this audit:** Lead (DeepSeek)
- **Date:** 2026-10-07 22:30 UTC
- **Repo HEAD at audit:** 6e8bd15 (T05 head — the provably out-of-sample runner)
- **Verdict:** **PASS (methodology)** — the calibration measurement/fitting
  harness is correct, deterministic, leakage-separated, and independently
  reproduced to 1e-12. **Publication caveat:** every number is SYNTHETIC; no real
  XAUUSD data exists in this container, so no calibrated value may be published as
  a market probability yet (RULE C at the system level remains gated on real
  data). RULE B (cost tiers) is absent and is flagged, not hidden.

---

## Claim

Lead, `coordination/deepseek/comm.md` 22:16 UTC, T11 go/no-go:

> proceed on the T05 head. Confirm ECE/Brier are measured on provably
> out-of-sample data, coverage is reported per tier (RULE D), no tuning on 2025,
> and — critically — that the value is labelled a **score** if ECE >= 0.05
> (RULE C). Record the honest number whatever it is.

Subject: `run_calibrated` (T05, commit `6e8bd15`), which fits a logistic base on
development, a calibrator on validation, and evaluates OOS once.

Acceptance: calibration measured and reported before any probability output; ECE
target < 0.05, failure > 0.10; Brier better than the 0.25 uninformative baseline;
coverage honesty (RULE D); results under the three cost tiers (RULE B).

---

## Evidence inspected

- **Commit:** `6e8bd15`
- **Files:** `src/models/{calibration,calibrators,calibrated}.py`,
  `src/models/demo_calibrated.py`, `tests/models/test_*.py`.
- Agent-D reran the suite and re-derived Brier/ECE from a **from-scratch**
  implementation (no import of `src.models.calibration`).

```
$ python3 -m unittest discover -s tests/models -t .   -> 162/162 OK
$ PYTHONPATH=. python3 /tmp/t11_probe.py
rows dev=799 val=799 oos=399
[platt]     OOS calibrated: brier=0.034226 (my=0.034226 match=True) ece=0.022154 (my=0.022154 match=True) skill=+0.8631
[isotonic]  OOS calibrated: brier=0.033860 (my=0.033860 match=True) ece=0.024551 (my=0.024551 match=True) skill=+0.8646
[histogram] OOS calibrated: brier=0.045971 (my=0.045971 match=True) ece=0.014456 (my=0.014456 match=True) skill=+0.8161
fit independent of OOS? True (all methods)
coverage platt: low:n=199,cov=0.499,acc=0.035,conf=0.029 | medium:n=26,cov=0.065,acc=0.538,conf=0.471 | high:n=174,cov=0.436,acc=0.983,conf=0.964
DATA: synthetic (no real XAUUSD present)
```

---

## Verification steps

1. **Score vs probability (V4-14).** The base model emits raw scores; the
   calibrator maps them to probabilities. `development` is reported **raw only**
   (no calibrated-dev row), because calibrating on the rows you score is
   tautological. PASS.
2. **Calibration method present and named.** Platt (Newton + ridge), isotonic
   (PAVA), histogram (equal-width). Each is fit on the validation partition, and
   `run_calibrated` structurally rejects any overlap or chronological inversion
   (`assert_partitions_separated`, verified in the T05 re-audit). PASS.
3. **ECE reproduced.** My independent equal-width ECE (10 bins, sample-weighted
   |acc−conf|) matches the reported ECE to **< 1e-12** for all three methods.
   Empty bins contribute nothing (verified by inspection of
   `reliability_diagram`). PASS.
4. **Brier reproduced.** Independent `mean((p−y)^2)` matches reported Brier to
   < 1e-12. All three beat the 0.25 baseline; Brier skill ≈ +0.82 to +0.86.
   Computed on the SAME OOS sample, not assumed. PASS.
5. **Coverage honesty (RULE D).** `coverage_analysis` reports, per tier, count,
   coverage, accuracy, and mean probability; empty tiers are returned with
   count 0 (visible, not hidden). On the synthetic OOS: platt high-tier
   cov=0.436, acc=0.983 vs conf=0.964 — the high-confidence subset is NOT passed
   off as the whole sample; coverage is always adjacent to accuracy. PASS.
6. **No lookahead in the calibration fit.** Removing the OOS partition leaves the
   validation metrics **byte-identical** (calibrator fit is independent of OOS).
   The split is chronological: development 2021–2022, validation 2023–2024, OOS
   2025 (verified from timestamps). No shuffling, no seed. PASS.
7. **No tuning on 2025.** No model-selection code touches the OOS years; OOS is
   scored once and never fed back. The only 2025 references are the split
   definition and the synthetic demo generator. PASS.
8. **Independent reproduction.** Reran the owner's tests from clean: 162/162 OK;
   the demo reproduces the same numbers. Did not trust the claimed counts. PASS.
9. **Honesty / vocabulary (MISSION §8).** The module docstring and demo banner
   state explicitly that this is a synthetic pipeline check, not a market result,
   and that output is uncalibrated/unpublished until T11. PASS.

---

## Honest numbers (synthetic, OOS n=399)

| method | Brier | ECE | Brier skill | meets_target | RULE C label |
|---|---|---|---|---|---|
| platt | 0.0342 | 0.0222 | +0.863 | yes | probability (if real) |
| isotonic | 0.0339 | 0.0246 | +0.865 | yes | probability (if real) |
| histogram | 0.0460 | 0.0145 | +0.816 | yes | probability (if real) |

All three ECEs are < 0.05 (target) and none exceed 0.10 (failure). By the RULE C
gate as stated, a value with ECE < 0.05 is a probability, not a score.

**But this is synthetic data.** The base model and the signal are generated by
`_synthetic_series`; a well-calibrated result here is a check that the *pipeline*
is wired correctly, not evidence about XAUUSD. The honest conclusion is: the
harness can produce a calibrated probability when fed data; whether that survives
on real market data is unknown and untested.

---

## Result

**PASS (methodology).** The calibration surface is correct, deterministic,
leakage-separated, and reproducible; coverage is reported per tier; nothing was
tuned on 2025. The measurement is trustworthy.

**Publication is NOT authorised.** No real data exists in this container; the
result is synthetic. Per RULE C the calibrated value must not be published as a
market probability until it is reproduced on real XAUUSD data. This is the same
blocker class as T03/T12/Q2, now confirmed for the calibrated path too.

## Notes

- **RULE B — flagged, absent.** No cost-tier model exists anywhere in `src/`
  (`baseline.py` states cost tiers are not applied; README notes "no cost tiers
  yet"). E04 remains OPEN. A decision-grade result cannot be claimed until the
  three cost tiers exist. Not silently omitted.
- **Calibrator selection untuned.** All three are reported; no choice is baked in.
  Correct for an audit; selection is a post-T11, post-real-data decision.
- **T05 head.** Audit is on `6e8bd15` (the F1 fix). If T05 advances further before
  the Lead flips it to DONE, re-audit the delta.
- **Reproducibility.** Numbers reproduce from the repo alone (synthetic); they do
  not reproduce from any real dataset because none is present.
- **Independence.** Agent-D authored none of the audited code. Probe in `/tmp`,
  uncommitted.
