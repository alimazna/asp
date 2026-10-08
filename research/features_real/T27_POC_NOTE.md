# T27 - PROOF-OF-CONCEPT calibration note (single window)

> **PROOF-OF-CONCEPT - single window.** Corpus: MT5 XAUUSD M1 export,
> **2026-06-24 .. 2026-10-08** (100,008 M1 bars; 6,670 FeatureSets / 6,669
> examples). A single window has **no out-of-sample**; **no walk-forward is
> claimed**. Owned by Agent-B (T27). Verdict artifact:
> `research/features_real/t27_poc_report.json`.

## Config
- Corpus: `research/features_real/corpus/real_corpus.json.gz`
- Partition: `--partition-mode fraction` (single-window; dev 3,997 / val 1,333 / OOS 1,339)
- Calibrator: Platt; horizon 1; **default ridge `l2=1e-6`** (the directive asked for
  the default so no ridge-knob argument is needed; the Lead separately confirmed at
  `l2=0.01` that the result is ridge-invariant).
- **No walk-forward run.**

## Result (calibrated OOS, n=1,339)

| Metric | Value |
|---|---|
| Brier | **0.24995** |
| Brier skill (vs base rate) | **+0.0002** |
| ECE (10 bins) | **0.0018** |
| MCE | 0.0018 |
| Directional accuracy | **0.5078** |
| Raw (uncalibrated) Brier / ECE | 0.2909 / 0.1778 |

**Reliability diagram** - all OOS mass in one bin:

| Bin | n | mean predicted | empirical | gap |
|---|---|---|---|---|
| [0.40, 0.50) | 1339 | 0.4904 | 0.4922 | +0.0018 |

**Coverage per tier**

| Tier | coverage | n | accuracy | mean_p | gap |
|---|---|---|---|---|---|
| low (<1/3) | 0.000 | 0 | - | - | - |
| medium (1/3..2/3) | 1.000 | 1339 | 0.492 | 0.490 | +0.002 |
| high (>=2/3) | 0.000 | 0 | - | - | - |

**Coverage at decision thresholds** (RULE D confident subset)

| Threshold | coverage | n |
|---|---|---|
| p>=0.55 | 0.0000 | 0 |
| p>=0.60 | 0.0000 | 0 |
| p>=0.65 | 0.0000 | 0 |

Empty tiers are reported, **not hidden** (RULE D).

**Directional (LONG p>=0.5 / SHORT p<0.5)**

| Direction | coverage | n | accuracy | mean_p | brier |
|---|---|---|---|---|---|
| LONG | 0.0000 | 0 | - | - | - |
| SHORT | 1.0000 | 1339 | 0.5078 | 0.4904 | 0.2499 |

## RULE C verdict
**PROBABILITY (formally) - SCORE in practice.** ECE 0.0018 < 0.05, so the
calibrated value formally clears the probability gate (pending the T29 audit).
But discrimination is ~zero: skill +0.0002, directional accuracy 0.508, **no
prediction reaches 0.55** (LONG coverage 0). The tiny ECE is trivially earned
against a ~0.49 base rate; it carries **no directional information**. The API
withholds the probability (`signal.probability = null`,
`probability_calibrated = false`); show the **score**.

## RULE E - honest negative
This is a **coin flip that is honest about being a coin flip**. The pipeline
calibrates; the model does **not** predict direction on XAUUSD in this window.
Nothing here is a demonstrated edge and must not be read as one.

## Appendix (beyond accepted scope, not the verdict)
The Dukascopy 2021-2025 multi-year run (`t27_decision_report.json`) reaches the
same honest conclusion (OOS 2025 ECE 0.0015, skill +0.0012). Retained as an
appendix and cross-check only.
