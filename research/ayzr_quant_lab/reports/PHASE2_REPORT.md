# AYZR Phase II — Advanced Gold Market Research Report

**Scope.** Independent audit of Phase I, then extended research into LONG, and a
search for genuinely new edge in SHORT/MEDIUM. Everything runs offline against
real data; ASTRA CORE is untouched (frozen digest `aee61fa9...`).

**Headline.** The audit found a **bug in Phase I's own experiments** that
invalidated its headline LONG number, and the corrected LONG does **not** survive
multiple-testing correction. SHORT and MEDIUM have **no edge** on price, macro, or
new microstructure hypotheses. This report says so plainly.

---

## A. Audit of the Phase I research

| # | Check | Result |
|---|---|---|
| A1 | Raw corpus checksums | ✅ `.csv.gz` + samples verify (raw `.csv` absent by design, gitignored) |
| A2 | C++ oracle freshness | ✅ rebuilt from current source; digest == frozen |
| A3 | LONG per-year reproduces | ❌ **NO** — see below |
| A4 | 2022 vs 2025 duplicated? | ✅ **No** — 0 overlapping entry timestamps; "identical" was a *rounding* artefact |
| A5 | Expectancy / PF / total-R | ✅ independent recompute agrees to 1e-6 |
| A6 | Bootstrap method | ⚠️ Phase I used iid only; block bootstrap added (both cross zero) |

### A3 — the bug (the most important finding)

Phase I's `reports/final_validation.json` reported LONG as **n=301, +0.076R, PF 1.12**.
Re-running the *identical* configuration now gives **n=442, +0.031R, PF 1.07**.

**Root cause, proven:** the Phase I experiments ran against the **pre-fix
`align_grid`**. That function originally emitted a possibly-still-forming trailing
bucket; it was corrected during Phase I (`tests/test_backtest.py` caught it). The
buggy grid yields **1499** H4 bars for 2021 vs **1555** correct — a different bar
grid, hence different trades. `final_validation.py` started *before* the fix and
Python had cached the old module, so **every Phase I experiment used the lookahead-
prone grid**. All Phase II numbers use the corrected grid.

### A4 — the "2022 = 2025" flag, resolved

Trade-level entry timestamps: 2022 has 93 entries, 2025 has 102, **overlap = 0**.
They looked identical in Phase I's table only because both rounded to
`n=63, win=0.381`. There was no data duplication, year-misalignment, or JSON bug.
(The rounding hid a real 63-vs-63 coincidence in the *buggy* grid; the corrected
grid separates them entirely.)

---

## B. Data sources (all free / open)

| Dataset | Source | License | Coverage | Rows |
|---|---|---|---|---|
| XAUUSD M1 BID+ASK | Dukascopy (dukascopy-node) | public datafeed | 2021–2025 | ~1.70M |
| XAUUSD M1 | operator MT5 export | owner's own | 2026-06-24..2026-10-08 | 100,008 |
| DGS10 / DGS2 / DFII10 | FRED (US Treasury / TIPS) | public domain | 1962/2003→ | 16k / 12.6k / 5.9k |
| DTWEXBGS | FRED broad USD index | public domain | 2006→ | 5.2k |
| VIXCLS | FRED / CBOE | public domain | 1990→ | 9.3k |
| GC=F, SI=F, DX-Y.NYB | Yahoo Finance chart API | research use | 10y daily | 2.5k each |
| CFTC COT (disagg.) gold | cftc.gov | public domain | 2020–2025 | 313 weekly |

No paid data, no scraping behind paywalls. Point-in-time lagging is enforced in
`ayzr_lab/external.py` (FRED +1 business day; COT Tuesday→next Monday).

**COMEX futures depth / true order flow: not available.** There is no free,
legal central order book for spot XAUUSD (OTC), and Yahoo/FRED provide no tick or
depth data. This was **not** fabricated from candles.

---

## C. LONG results

Corrected baseline (H4, trend gate, 2.0/2.0 ATR, one active trade):

| Year | n | win | expR | totalR |
|---|---|---|---|---|
| 2021 | 74 | 0.514 | −0.004 | −0.29 |
| 2022 | 93 | 0.516 | −0.002 | −0.19 |
| 2023 | 91 | 0.538 | +0.045 | +4.06 |
| 2024 | 82 | 0.512 | −0.010 | −0.85 |
| 2025 | 102 | 0.569 | +0.109 | +11.13 |
| **Pooled** | **442** | **0.532** | **+0.031** | **+13.86** |

**Decomposition (the honest diagnosis):**

- **Direction:** BUY n=271 **+0.102R**; SELL n=171 **−0.081R** → it is a
  **long bias in a gold bull market**, not a symmetric edge.
- **Concentration:** the **top 10 of 442 trades produce 74%** of all profit.
- **Cost sensitivity:** spread ×2 → +0.012R; ×3 → **−0.007R**; slippage 0.5 →
  ≈0. It is **cost-fragile**.
- **Period:** only 2025 is meaningfully positive; 2021/22/24 are flat-negative.
- **Macro conditioning (new):** `dxy_falling_5d` improves H4 in **both** slices
  (train +0.084, OOS +0.063); `vix_low` and `cot_mm_long` also positive both
  slices — but n is only 118–228 and the long-bias remains.

**Overfitting verdict** (`reports/overfitting_audit.json`, 90-config family):

| Test | Value | Reading |
|---|---|---|
| Deflated Sharpe prob | **0.035** | observed Sharpe (0.032) < expected max from 90 random trials (0.119) |
| PBO (CSCV) | **0.50** | coin-flip that the IS-best holds OOS |
| Holm survivors (α=0.05) | **0 / 90** | none |
| Bootstrap (iid / block) | [−0.057,+0.117] / [−0.049,+0.111] | both cross zero |

**Frozen 2026 holdout** (never used for tuning): **+0.090R over 23 trades** —
positive but far too few to confirm; SELL actually beat BUY there, consistent
with the period-dependence finding.

**LONG verdict: promising but unproven.** It is a long-biased, concentrated,
cost-fragile effect that fails multiple-testing correction. It is **not** a
validated edge.

### C.1 Engine optimization (exit management)

The concentration finding motivated a trade-management study
(`experiments/time_stop_optimization.py`, `experiments/engine_optimization.py`).
Only the timeout horizon helped; breakeven, ATR-trailing, and partial-TP all
*hurt* (they cap the fat winners the edge depends on).

| max_bars | train expR | OOS expR | pooled expR | PF |
|---|---|---|---|---|
| 200 (spec) | +0.014 | +0.056 | +0.031 | 1.07 |
| 40 | +0.024 | +0.063 | +0.040 | 1.09 |
| 20 | +0.021 | +0.062 | +0.038 | 1.09 |
| **12** | **+0.027** | **+0.087** | **+0.052** | **1.14** |
| 8 | +0.012 | +0.084 | +0.041 | 1.12 |

`max_bars=12` improves every slice and **halves the concentration** (top-10
profit share 74% → 39%). But it is still **not conclusive**: block-bootstrap CI
[−0.019,+0.121] crosses zero and Deflated Sharpe prob is 0.13 (up from 0.035).
It is a genuine robustness improvement, not proof of edge.

---

## D. SHORT and MEDIUM results

Baseline (price-only) is negative in train **and** OOS. Every additional
information source was tested:

| Information source | M5 (SHORT) | M15 (MEDIUM) |
|---|---|---|
| ASTRA signal + filters (Phase I) | negative both slices | negative both slices |
| MTF alignment / mean-revert / regime | negative both slices | negative both slices |
| Macro: real yield / DXY / VIX / COT | negative both slices | negative both slices |
| New: Bollinger squeeze, vol-expansion, failed-breakout, opening-range | negative both slices | negative both slices |

Representative new-hypothesis numbers (train / OOS expR):

| Hypothesis | M5 | M15 |
|---|---|---|
| Bollinger squeeze breakout | −0.320 / −0.199 | −0.207 / −0.073 |
| Volatility-expansion | −0.310 / −0.172 | −0.146 / −0.118 |
| Failed-breakout reversal | −0.244 / −0.177 | −0.151 / −0.129 |
| Opening-range breakout | −0.186 / −0.067 | −0.094 / −0.036 |

**No new edge was found.** The intraday friction (spread ≈ 31% of M5 ATR) plus
the absence of a real order-flow signal makes these timeframes uneconomic on the
data available.

**SHORT / MEDIUM verdict: no edge currently.** No order flow, no depth, no
historical news archive with exact timestamps was available, so the one remaining
hypothesis class could not be tested and is not claimed.

---

## E. Statistical summary

- Sample sizes: LONG 442 (2021–25) + 23 (2026); SHORT/MEDIUM thousands each.
- All headline CIs are block-bootstrap, not iid.
- Multiple testing corrected (Holm + Deflated Sharpe + PBO).
- OOS never used to select parameters; selection used train only.
- Cost sensitivity reported for every engine.

---

## F. Software changes

New (all under `research/ayzr_quant_lab/`):
`ayzr_lab/{stats2,trades,external}.py`,
`data_sources/{fetch_external,fetch_cot}.py`,
`experiments/{long_deepdive,external_ablation,intraday_microstructure}.py`,
`validation/{audit_phase1,overfitting_audit,holdout_2026}.py`,
`engines/horizon_engines.json`, `docs/TEAM.md`, `datasets/*`.
`src/` **untouched**; ASTRA protection gate still passes.

---

## G. Final classification

| Engine | Classification |
|---|---|
| **LONG (H4)** | **Promising but unproven** — positive but fails overfitting correction; long-biased; cost-fragile; **max_bars=12 time-stop materially improves it (PF 1.07→1.14, concentration halved) but still not conclusive** |
| **MEDIUM (M15)** | **No edge currently** — negative across all price, macro, and new hypotheses |
| **SHORT (M5)** | **No edge currently** — same, and spread makes it structurally hard |

---

## H. Recommendations

1. **LONG → shadow only, do not promote.** Its 2026 holdout (+0.09R/23 trades) is
   too thin. Before any promotion, collect ≥100 *live forward* H4 trades and
   require Deflated-Sharpe prob > 0.95 with a positive down-trend sample.
2. **Do not spend more time tuning SHORT/MEDIUM on OHLCV or macro data.** Four
   independent hypothesis families already failed. Further parameter work is
   expected to overfit, not to help.
3. **The only promising path for SHORT/MEDIUM is data we do not yet have:**
   a licensed COMEX/CME order-flow or depth feed, or a timestamped news/economic-
   surprise archive. Either requires the owner's decision and likely a paid source
   — explicitly **not** purchased here.
4. **Keep the macro conditioning lead** (`dxy_falling_5d` on H4) as a research
   feature, but validate it on more forward data before trusting it.
5. **Stop** trying to make three engines active because they are named. One
   honest "promising but unproven" engine is worth more than three fictional ones.

---

## Reproduce

```bash
cd research/ayzr_quant_lab
python3 validation/audit_phase1.py
python3 validation/overfitting_audit.py
python3 experiments/long_deepdive.py
python3 experiments/external_ablation.py
python3 experiments/intraday_microstructure.py
python3 validation/holdout_2026.py
python3 tests/test_astra_protection.py
```
