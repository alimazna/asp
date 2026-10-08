# AUDIT — T27 real-data calibration (PROOF-OF-CONCEPT, single window)

- **Date:** 2026-10-08
- **Auditor:** **DeepSeek (Lead)** — *substitute*. The designated auditor, **Agent-D**,
  went **STALE** (no heartbeat/commit after 09:45 UTC; >75 min at close) and did not
  file this audit. Rather than stall the human-directed close or fabricate an
  auditor, the Lead performed an **independent reproduction** and records the
  liveness gap here. This is honest evidence, not a forged signature.
- **Artifact under audit:** `research/features_real/t27_poc_report.json` and
  `research/features_real/T27_POC_NOTE.md` (Agent-B, commits `669809d`/`840560c`).
- **Scope:** PROOF-OF-CONCEPT — single 3.5-month window; **no walk-forward claim**.

## 1. Independent reproduction

The Lead reproduced the POC metrics from the source on a **separate code path**
(`research/reports/tools/poc_metrics_lead.py`), independently of Agent-B's report
generator: `load_corpus` -> `build_labeled_examples(horizon=1)` -> `fractional_split`
-> `fit_logistic` -> `fit_calibrator("platt")` -> metrics.

| Metric (calibrated OOS, n=1339) | Agent-B | Lead | Match |
|---|---|---|---|
| Brier | 0.249945 | 0.249946 | ✅ |
| Brier skill | +0.000218 | ≈ -0.00003* | ✅ (both ≈ 0) |
| ECE | 0.001780 | 0.001699 | ✅ |
| MCE | 0.001780 | 0.001699 | ✅ |
| Directional accuracy | 0.507842 | 0.507842 | ✅ |
| Reliability bins | 1 (bin [0.40,0.50)) | 1 (bin [0.40,0.50)) | ✅ |
| Coverage p≥0.55 / 0.60 / 0.65 | 0 / 0 / 0 | 0 / 0 / 0 | ✅ |
| LONG / SHORT n | 0 / 1339 | 0 / 1339 | ✅ |
| Split dev/val/oos | 3997/1333/1339 | 3997/1333/1339 | ✅ |

\* Trivial difference from the ridge term (`l2=0.01` vs the report's default
`1e-6`); both are ≈ 0, and the Lead separately showed on the multi-year corpus that
the ridge is a **speed** knob, not a result knob. Every substantive number matches.

## 2. Adversarial checks

- **No lookahead / causality:** `build_labeled_examples` labels each instant from the
  *next* bar and drops the last `horizon`; `fractional_split` is chronological,
  disjoint, ascending, and asserts causality. **No leakage found.**
- **No OOS tuning:** the model is fit on dev, the Platt calibrator on val; OOS is
  touched once for measurement. `--l2` was *lowered* from 0.01 to the default; the
  result is unchanged, so no knob was tuned on OOS. **Clean.**
- **Reliability diagram:** adversarially, the tiny ECE is **not** evidence of skill.
  All 1,339 OOS predictions lie in one bin [0.40,0.50) with mean 0.4904 vs observed
  0.4922. The model predicts ≈ the base rate for every decision. **The ECE is
  degenerate, not earned.**
- **Tier thresholds:** 0.55/0.60/0.65 coverage is **0/0/0** — the model never
  produces a confident call. Reported, not hidden.
- **LONG vs SHORT:** every OOS decision is below 0.5 (SHORT-side), accuracy 0.5078
  ≈ base rate. There is **no** discriminating subset.

## 3. Verdict

**SCORE** (formally the value passes the ECE gate, but it must be surfaced as a
score, not a probability).

- RULE C: ECE **0.0018 < 0.05** → the *number* is a calibrated probability; the
  directive's failure threshold ECE > 0.10 is not reached.
- **But discrimination ≈ 0** (skill ≈ 0; directional accuracy 0.508; zero coverage
  above 0.55). A "probability" that is always ≈ 0.49 carries no decision content.
  **Surface it as a score**; the API already withholds it
  (`probability_calibrated=false`, gate closed — T13 replay transcript).

## 4. Findings

| ID | Severity | Finding | Status |
|---|---|---|---|
| A27-1 | info | ECE is degenerate (single bin at the base rate) — must not be read as skill. | accepted, stated in FINAL_REPORT |
| A27-2 | info | Zero coverage above p=0.55; LONG coverage 0. | accepted, reported |
| A27-3 | **process** | Designated auditor Agent-D went STALE; Lead substituted. | recorded; liveness gap |
| A27-4 | info | Raw (uncalibrated) OOS Brier 0.2909 / ECE 0.1778 → calibration does real work. | accepted |

**Audit result: PASS** for the POC scope, with the honest-negative headline stated
plainly. **Not** evidence of an edge; **not** out-of-sample.
