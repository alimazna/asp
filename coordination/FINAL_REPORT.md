# FINAL REPORT — AURA / ASTRA (XAUUSD decision-support backend)

> **PROOF-OF-CONCEPT — single window.** The calibration result below is computed
> on a **single 3.5-month window** (2026-06-24 .. 2026-10-08). A single window has
> **no out-of-sample**; **no walk-forward is claimed**. This is an honest POC, not
> a publication of a trading edge.
>
> Lead: DeepSeek (agent). Repo: `alimazna/asp` @ `main`. Mode: **SHADOW only, no
> live trading**. Baseline controls and `docs/archive/` untouched.

---

## 1. Mission summary

Build **AURA** (technical) / **ASTRA** (product): a decision-support backend for
**XAUUSD** that ingests market bars, computes a multi-timeframe feature snapshot,
produces a **calibrated probability**, and serves it over a **frozen, loopback-only
v1 API** that a frontend can build against — in **SHADOW mode**, with no live
trading, no lookahead, and no unearned "edge" claims.

The mission's completion bar (§10.7) is **honesty, not profit**: a stable API, a
complete frontend handoff, and an **honest calibration result** — reported as a
**probability** only if it clears the calibration gate, otherwise as a **score**.

**Status: COMPLETE.** Every gate is met. The headline is an honest negative.

## 2. What was built (inventory)

| Component | Artifact | State |
|---|---|---|
| Feature engine | C++ `AnalyticalFeatureEngine` (multi-TF), frozen `FeatureSet` shape, causality/no-lookahead | green, 13 feature tests |
| Data path | M1 → feature harness; configurable path (env/config, no hardcoding) | green (T26/T28) |
| Model + calibration | ridge logistic + Platt/isotonic/histogram, Brier/ECE/reliability/coverage | green (T27) |
| Cost tiers (RULE B) | `src/models/costs.py` 3-tier cost model | audited PASS (E04) |
| Decision model | horizon + SL/TP, cost-aware direction labels | green (T15) |
| Backend host | `aura_backend_host`: startup/shadow lifecycle, persistence | green (T09/T11) |
| Analysis API | **frozen v1 contract**, 15 routes, loopback-only, tag `api-v1.0` | frozen + machine-checked (T17) |
| Mock | `scripts/mock_api.py`, same schema, `--check` 0 failures | green (T18/T19) |
| Frontend handoff | field-by-field guide for all 15 routes | delivered (T16) |
| MT5 bridge | loopback bridge process, replay path | green (T13) |

## 3. Data — source, window, bars, limitations

- **Source:** operator MetaTrader 5 XAUUSD **M1** export, converted with a stdlib
  tool (`research/data/xauusd_m1/tools/convert_mt5.py`) to a canonical, sorted,
  deduplicated CSV, validated by `quality_check_mt5.py` → `QUALITY.md`.
- **Window:** **2026-06-24 11:08 .. 2026-10-08 10:30** (broker time). Single window.
- **Bars:** **100,008** M1 bars. **0** duplicates, **0** OHLC violations, **0** NaN,
  **0** off-grid timestamps.
- **Features:** **6,670** frozen `FeatureSet`s (**2,497** valid; the remainder are
  honest `INCOMPLETE` warm-up sets at the window start — nothing fabricated).
- **Price-band WARN:** observed 3942..4697 USD/oz, outside the directive's
  1800..3000 band — a real gold move, **reported, not repaired**.

**Limitations (data).** One provider, one ~3.5-month window, one regime. It cannot
support a multi-regime chronological dev/val/OOS split or a walk-forward; any
in-window split is **in-sample-ish** evidence.

> **Appendix (beyond accepted scope).** A multi-year Dukascopy 2021-2025 corpus
> (1,695,651 M1 bars, BID+ASK, 68 venue closures listed) was also acquired,
> validated, and calibrated (OOS Brier 0.2497, ECE 0.0015, accuracy 0.517; skill
> ≈ +0.0012). It is **not the verdict** for this mission — the human accepted the
> 3.5-month corpus. It is retained as an appendix and a cross-check.

## 4. Calibration result (PROOF-OF-CONCEPT — single window)

Corpus: `real_corpus.json.gz` (MT5, 6,670 sets / 6,669 examples). Single-window
causal split by fraction: **dev 3,997 / val 1,333 / OOS 1,339**. Platt calibration
fitted on validation, evaluated once on the OOS tail. Independent Lead reproduction
(identical to Agent-B's T27):

| Metric (calibrated OOS) | Value |
|---|---|
| Brier score | **0.24995** |
| Brier skill (vs base rate) | **≈ 0.000** |
| ECE (10 bins) | **0.00170** |
| MCE (worst bin) | **0.00170** |
| Directional accuracy | **0.508** (coin-flip) |
| LONG coverage | **0** |
| SHORT coverage | 1,339 (all) |
| Coverage at p≥0.55 / ≥0.60 / ≥0.65 | **0 / 0 / 0** |

**Reliability diagram** — every OOS prediction lies in the **single bin [0.40, 0.50)**:

| Bin | n | mean predicted | empirical rate | gap |
|---|---|---|---|---|
| 0.40–0.50 | 1,339 | 0.4905 | 0.4922 | +0.0017 |

Raw (uncalibrated) OOS Brier 0.2915, ECE 0.1774 — calibration **is** doing real
work (ECE 0.177 → 0.0017), but the base model never becomes confident.

**Read this honestly.** ECE is tiny **because the model never leaves the base rate**.
It predicts ≈0.49 for every decision; that is trivially well-calibrated against a
≈0.49 event rate, and it carries **no directional information**. The tiny ECE is not
evidence of skill — it is evidence of a coin flip that is honest about being a coin
flip.

## 5. Verdict

**PROBABILITY — formally; SCORE in practice.**

- **RULE C gate: PASS.** The headline calibration metric is ECE = **0.0017 < 0.05**
  (the directive's failure threshold is ECE > 0.10, not reached). So the calibrated
  value is, formally, a **probability**.
- **But the discrimination is ~zero** (skill ≈ 0.000; directional accuracy 0.508;
  no prediction above 0.55). The value must therefore be surfaced **as a score**:
  the API already withholds the probability (`signal.probability = null`,
  `probability_calibrated = false`, `meta.score_is_probability = false`, gate
  **closed** — see the T13 replay transcript).

**One-line verdict: a correctly-wired, honestly-calibrated probability pipeline
whose model has no demonstrated predictive edge on XAUUSD in this window.**

## 6. Limitations (honest)

1. **Single 3.5-month window, single regime.** No out-of-sample; no walk-forward.
2. **Zero confident predictions.** Coverage above 0.55 is 0; the model cannot
   discriminate up vs down.
3. **Calibration is trivial, not earned.** All mass in one bin near the base rate.
4. **MCE = ECE = 0.0017** here only because there is one bin; this is a degenerate
   reliability diagram, not a strong calibration result.
5. **Warm-up:** 4,173 of 6,670 feature sets are `INCOMPLETE` (thin history).
6. **OK-labelled features:** several feature slots are `null`/`UNKNOWN` and are
   surfaced as *unavailable*, never imputed.
7. **No live trading** is implemented or authorized; SHADOW only.

## 7. What is ready for the frontend

The frontend can build **today** against the **frozen v1 contract** using
`scripts/mock_api.py` (offline), then swap to the live loopback host. Ready:

- All **15** routes, frozen and machine-checked (mock **and** real façade validated
  against `API_V1_SCHEMA.json`).
- Field-by-field interpretation guide (`docs/frontend/FRONTEND_HANDOFF_GUIDE.md`),
  including the `null`/`UNKNOWN` = *unavailable* rule.
- Error/state handling documented: OFFLINE / DEGRADED / STALE; `capability_impact`
  lists what each missing input removes.
- The **display rule** is settled: show a **score**, not a probability;
  `signal.probability_calibrated` is the single source of truth.
- The real end-to-end replay transcript is committed
  (`research/reports/t13_realdata.md`).

## 8. What remains unproven

- **Any predictive edge.** Discrimination is ~0. Not demonstrated here, and this
  window cannot demonstrate it.
- **Out-of-sample behaviour** across regimes — not testable on one window.
- **Confident-signal subsets** — zero coverage above p=0.55; untested.
- **Feature payoff:** whether the multi-TF feature set carries signal at all is
  **not shown**. (The appendix multi-year run reaches the same honest conclusion.)
- **Model class:** only ridge logistic was exercised on real data; GBT etc. remain
  unvalidated on real data.

## 9. Recommendation

1. **Ship the backend now** as a **SHADOW decision-support** system: stable frozen
   API, complete handoff, honest calibration. Do **not** wire it to live execution.
2. **Surface a score, not a probability** in the UI (`probability_calibrated=false`).
   The frontend contract already supports this.
3. **Do not market a signal.** The honest result is no demonstrated edge; any claim
   to the contrary would be false.
4. **To earn a real result,** the next phase needs **multi-year, multi-regime data**
   with a genuine out-of-sample walk-forward, feature/qc work to create
   discrimination (currently zero), and a model comparison. The value to the
   operator today is a **correct, honest, well-engineered pipeline**, not alpha.
5. **Keep labelling POC** until a walk-forward on multi-year data passes.

---

### Verification evidence

| Suite | Result |
|---|---|
| C++ `ctest` | 19/19 pass |
| Frozen-contract v1 e2e (T24) | 88/88 |
| Analysis-API schema fixtures (T22) | 52/52 |
| Mock API (T19) | 39/39 |
| Contract checker (T16) | 36/36 |
| T28 data paths | 20/20 |
| T30 shape guard | 19/19 |
| Models suite | 298 OK |
| Features suite | 14/14 |
| **T13 evidential (real host + real data)** | **96/96 PASS** |
| Mock `--check` | 0 failures |
| **T13 real-data replay transcript** | `research/reports/t13_realdata.md` |
| **T29 audit (POC)** | `AUDIT_REPORTS/AUDIT-T27-realdata-2026-10-08.md` — **Lead-substitute**: Agent-D went STALE; the Lead reproduced independently and recorded the liveness gap. |

### Open process item (honest)

The designated independent auditor, **Agent-D**, went **STALE** (no heartbeat after
09:45 UTC) and did not file the POC audit. To honor the human's "finish now"
directive without fabricating an auditor, the **Lead** performed the independent
reproduction and filed it as a **substitute** audit, explicitly labelled as such.
A genuinely independent re-audit by Agent-D (or another agent) is still desirable
when it resumes — the substitution is disclosed, not hidden.

<!-- Drafted by the Lead (DeepSeek) agent, on behalf of the operator. -->
