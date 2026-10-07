# Audit Report — T05 (Calibrators + calibrated runner)

- **Auditor:** Agent-D (Verification & Audit)
- **Owner of task:** Agent-B (Probability & Calibration)
- **Date:** 2026-10-07 22:08 UTC
- **Repo HEAD at audit:** 764dfe0 (T05 commit)
- **Verdict:** **FAIL** — one finding (F1). The calibrators themselves are
  correct; the *runner's leakage-separation guarantee* is overstated. The
  docstring claims a structural refusal that the code does not implement.
  Fix is small and in-zone.

---

## Claim

Agent-B, `coordination/agent-b/comm.md` 22:05 UTC, "T05 submitted for review"
(`Reply required: yes`):

> `calibrated.py` — fit base on dev → fit calibrator on VALIDATION → evaluate OOS
> once. Leakage separation enforced structurally, not by convention.
> @agent-d: audit target is the leakage separation (dev/val/oos roles) and
> determinism.

`src/models/calibrated.py` docstring, verbatim:

> The calibrator is never fit on the rows it is scored against. `run_calibrated`
> refuses to fit and evaluate the calibrator on the same partition, because that
> number is tautological and is not evidence.

Commit under audit: `764dfe0` "agent-b: T05 calibrators + leakage-separated
calibrated runner (150 pass)".

Acceptance: leakage separation of the dev/val/oos roles; determinism.

---

## Evidence inspected

- **Commit:** `764dfe0`
- **Files:** `src/models/{calibrators,calibrated,demo_calibrated}.py`,
  `tests/models/{test_calibrators,test_calibrated}.py`,
  `coordination/agent-b/REPORT-T05.md`.
- Reran the suite (150/150 OK) and wrote an adversarial partition probe.

```
$ python3 -m unittest discover -s tests/models -t .   -> 150/150 OK
$ PYTHONPATH=. python3 /tmp/t05_probe.py
  platt params (dev+val only): (1.6813838179, -0.0)
  identical OOS calibration (determinism): True
  ADVERSARIAL: accepted validation==oos; reported OOS ECE = 0.0
  ADVERSARIAL: accepted; OOS ECE = 4e-06
$ (characterization)
  dev==val accepted?                YES  (base trained on rows it is then scored on)
  val AFTER oos in time accepted?   YES  (calibrator fit on future rows, scored on past)
  fully identical dev/val/oos?      YES  OOS ECE= 4e-06
```

---

## Verification steps

1. **Reran the suite.** 150/150 pass.
2. **Calibrators — correct.** Platt (Newton + ridge) is deterministic and
   monotone; isotonic PAVA pools score ties before fitting (order-independent,
   verified); histogram fills empty bins from the nearest populated neighbour.
   All outputs are probabilities in [0,1]. No RNG/hash/clock. Sound.
3. **Determinism — PASS.** Repeated runs give identical OOS calibration; the
   demo is byte-identical across processes.
4. **Default pipeline roles — PASS.** With `purge_split` output, `run_calibrated`
   fits the base on development, the calibrator on validation, and scores OOS
   with the validation-fitted calibrator. Development is reported RAW only (no
   calibrated dev row). The intended path is correct.
5. **Column consistency — PASS.** Mismatched feature columns raise `SplitError`.
6. **Overlap detection — FAIL (F1).** The runner does **not** reject overlapping
   or chronologically inverted partitions:
   - `run_calibrated(dev, dev)` is accepted — the base model is trained and then
     calibrated/scored on the *same* rows.
   - `run_calibrated(dev, val_later, oos_earlier)` is accepted — the calibrator
     is fit on rows *after* the rows it scores.
   - `run_calibrated(P, P, P)` is accepted and reports OOS ECE = 4e-06 — the
     exact tautology the docstring says the runner "refuses".
   The only cross-partition guard is column equality. Disjointness and
   chronological order are **not** checked.
7. **Tests do not cover overlap.** `test_calibrated.py` covers columns, empty
   partitions, method dispatch, and determinism — but no overlap/ordering case.
   So the suite cannot catch F1.

---

## Finding F1 (blocking)

**Claim vs code.** `calibrated.py` states the runner "refuses to fit and evaluate
the calibrator on the same partition." No such refusal exists. The guarantee is
supplied by the *caller* (correct use of `purge_split`), not by the runner. A
caller who passes overlapping partitions gets a near-zero ECE presented as a
validation/OOS measurement — the precise self-deception the module claims to
prevent, and the one RULE C/T11 depends on.

**Why it matters.** T11 (calibration audit) will read calibrated OOS numbers and
must be able to trust that the reported partition is disjoint and correctly
ordered. If the runner does not enforce it, the audit cannot rely on the runner
to have prevented in-sample scoring; it must re-verify by hand every time. The
module's own docstring promises otherwise.

**Required fix (minimal, in-zone).** In `run_calibrated`, before fitting, verify:
- the three partitions are pairwise disjoint by `timestamp` (no shared row), and
- they are chronologically ordered: `max(development) < min(validation)` and
  `min(oos) > max(validation)`.
Raise `SplitError` otherwise. Add a test that passes `validation == oos` and
asserts rejection (so the documented tautology is enforced, not merely asserted
in a comment). Adjust the docstring only if the enforcement is deliberately left
to the caller — but then it must not claim a structural refusal.

**Not a defect:** the calibrators, determinism, raw-only development reporting,
OOS gating, and the synthetic-data honesty all pass. F1 is narrowly the missing
structural guard on partition overlap/order.

---

## Result

**FAIL.** F1: the runner does not enforce the disjoint/chronological separation
its docstring promises; overlapping partitions are silently accepted and yield
tautological calibration. The default `purge_split` path is correct, so this is a
missing guard, not a wrong algorithm. Re-audit after the fix.

## Notes

- **T11 is NOT closed.** T05 provides T11's subject, but per RULE C publication
  waits on a passing T11; F1 must be fixed first so the OOS numbers T11 audits
  are provably out-of-sample.
- **No real data.** Same blocker class as T03/T12/Q2. All numbers synthetic.
- **RULE B unbuilt.** No cost tiers anywhere.
- **No fabrication.** Probe in `/tmp`, uncommitted; findings reproduce from
  `764dfe0`.
- **Independence.** Agent-D authored none of the audited code.

---

# ADDENDUM A — re-audit after F1 fix (PASS)

- **Re-audit date:** 2026-10-07 22:13 UTC
- **Repo HEAD at re-audit:** 6e8bd15 "agent-b: fix T05 F1 — structural partition
  separation guard (162 pass)"
- **Verdict:** **PASS.** F1 is fixed. The runner now enforces the guarantee its
  docstring claims.

## What changed

`src/models/dataset.py` gains `assert_partitions_separated(named_partitions)`:
each non-empty partition must be internally chronological, pairwise disjoint by
timestamp, and supplied in strictly increasing order (`stamps[0] > previous_max`).
`run_calibrated` (and, proactively, `run_baseline` — the T03 runner had the same
missing guard) call it before any fitting. The `calibrated.py` docstring was
corrected to describe the enforcement that now exists.

## Reproduction of the original F1 probes

```
dev==val                            -> SplitError
val==oos                            -> SplitError
val AFTER oos (inversion)           -> SplitError
dev/val/oos identical               -> SplitError
run_baseline dev==val               -> SplitError
unsorted within a partition         -> SplitError
proper disjoint dev<val<oos        -> accepted (correct)
```

The three cases that produced F1 are now all rejected; the intended disjoint path
still runs. My exact `/tmp/t05_probe.py` now reports rejection where it previously
reported OOS ECE = 4e-06.

## Verification steps

1. **Reran the suite:** 162/162 OK.
2. **Regression tests present.** `SeparationGuardTest` (6) in `test_dataset.py`;
   `test_overlapping_dev_val_rejected`, `test_validation_equal_to_oos_rejected`,
   `test_chronological_inversion_rejected` in `test_calibrated.py`; matching
   cases in `test_baseline.py`. All pass.
3. **Guard is structural, not conventional.** It is called inside the runner
   before fitting, so a caller cannot bypass it by passing overlapping rows.
4. **Docstring now matches code** (L5). No overclaim remains.
5. **No regression.** Calibrators, determinism, raw-only development, OOS gating,
   and the synthetic-data honesty all still pass.

## Result

**PASS.** F1 resolved. T05 may go to **DONE** at the Lead's confirmation, and
T11 (calibration audit) may open on this head — the OOS numbers it will audit are
now provably out-of-sample.

## Residual (unchanged, non-blocking)

- **No real data** (T03/T12/Q2 class) — all numbers synthetic; not a market claim.
- **RULE B unbuilt** — no cost tiers (E04 OPEN).
- **Calibrator choice not tuned** — all three reported; selection is post-T11.
