# AYZR Quant Lab — Research Report

**Question.** Can the SHORT / MEDIUM / LONG XAUUSD horizon engines be built into
honest, evidence-backed analyzers on top of the unmodified ASTRA CORE?

**Answer.** One of the three can, with caveats: **LONG (H4) shows a small,
consistent, but not statistically conclusive edge.** **SHORT (M5) and MEDIUM
(M15) are not viable on price-only data** — the ASTRA signal has no intraday
edge and friction makes it worse. This is reported as the finding, not hidden.

All numbers below come from the real Dukascopy M1 corpus (2021–2025, ~1.7M bars,
BID+ASK), a realistic backtester, and train (2021–23) vs out-of-sample (2024–25)
splits. Reproduce with the commands in `README.md`.

---

## 1. Data

| Item | Value |
|---|---|
| Source | Dukascopy public datafeed (BID + ASK), via `dukascopy-node 1.50.0` |
| Window | 2021-01-03 .. 2025-12-30 (UTC) |
| M1 bars | ~1.70M BID (ASK ~2.18M; intersection used for spread) |
| Quality | 0 OHLC violations, 0 duplicates, 0 non-monotonic, 0 zero-volume |
| Regimes | price 1680 → 4400 USD/oz — trending, ranging, volatile all present |
| Spread | median 0.356 (2021) → 0.58 (2025) USD/oz; 31% of M5 ATR, 4% of H4 ATR |

Data quality is documented in `research/data/xauusd_m1/QUALITY_dukascopy_2021_2025.md`.
No repainting, no synthetic rows.

## 2. Method (fidelity gates)

Before any conclusion, the research math was proven identical to ASTRA CORE:

- `tests/test_astra_port.py` — the Python port is diffed field-by-field against a
  **compiled C++ oracle** that links the real ASTRA engines. **1050/1050 checks pass.**
- `tests/test_pipeline.py` — the fast numpy path matches the scalar reference on
  real windows. **1552 checks pass.**
- `tests/test_backtest.py` — no-lookahead (next-bar fill), TP/SL ordering,
  worst-case ambiguous bars, spread/slippage sides, MAE/MFE, calendar resampling.
  **All pass** (this suite caught and fixed a partial-bucket lookahead bug in the lab).
- `tests/test_astra_protection.py` — freezes ASTRA CORE oracle output
  (sha256 `aee61fa9…`); any drift fails the gate.

## 3. ASTRA CORE baseline (the yardstick)

ASTRA CORE's naked signal + its 1.5×ATR stop / 2.5×ATR target geometry:

| Year | M15 n | M15 expR | M15 PF | H4 n | H4 expR | H4 PF |
|---|---|---|---|---|---|---|
| 2021 | 10401 | −0.181 | 0.74 | 704 | −0.031 | 0.95 |
| 2022 | 10916 | −0.172 | 0.75 | 771 | −0.018 | 0.97 |
| 2023 | 10266 | −0.159 | 0.77 | 813 | +0.075 | 1.13 |
| 2024 | 10437 | −0.116 | 0.83 | 682 | −0.019 | 0.97 |
| 2025 | 10968 | −0.061 | 0.91 | 788 | +0.021 | 1.03 |

**Finding:** ASTRA CORE M15 is negative-expectancy in all five years; H4 is
near-breakeven. The signal's ~37% win rate sits exactly at the break-even point
for a 1:1.67 reward/risk, and friction pushes intraday negative. The horizons
must therefore *add* edge, not merely re-tune.

## 4. Hypothesis sweep (single filters, train vs OOS)

Filters tested: trend-only, min-strength, RSI-extreme, session, no-volatile,
volatility band, and combinations. Key result (pooled expR):

| Entry | Best single filter | train expR | OOS expR |
|---|---|---|---|
| M5 | trend+RSI8 | −0.190 | −0.130 |
| M15 | trend+RSI8+session | −0.141 | −0.073 |
| H4 | trend-only | **+0.027** | **+0.034** |

**Finding:** no intraday filter combination turns M5/M15 positive in either
slice; only H4-with-trend is positive in both.

## 5. Exit-geometry grid (stop × target, 150 cells)

Selecting geometry by OOS is cheating, so the rule was: **positive in BOTH train
and OOS**. Result:

- **12 of 150 robust cells exist — all H4.** M5 and M15 have **zero** robust cells.
- M5/M15 "best OOS" cells were all *negative* in train (classic overfit trap,
  correctly rejected).

## 6. Alternative constructions (beyond ASTRA's momentum)

MTF-alignment, regime-trend, mean-reversion, and volatility-trend were each run
on M5/M15/H4:

| Entry | astra_raw | mtf_align | regime_trend | mean_revert | vol_trend |
|---|---|---|---|---|---|
| M5 | −0.19 | −0.14 | −0.13 | −0.20 | −0.13 |
| M15 | −0.09 | −0.04 | −0.07 | −0.13 | −0.08 |
| H4 | +0.002 | −0.03 | **+0.031** | −0.22 | — |

**Finding:** every intraday construction is negative. Even mean-reversion fails.
On H4, `regime_trend` (trade the regime trend, RSI must not oppose) is the
only robust positive.

## 7. Tightest-confluence stress (intraday)

Even the tightest simultaneous selectivity — trend + unanimity (3/3 votes) + RSI
extreme + London/NY session + higher-TF agreement — was tested for M5/M15:

| Entry | confluence | train expR | OOS expR |
|---|---|---|---|
| M5 | trend+unanimity+rsi8+session | −0.191 | −0.133 |
| M15 | trend+unanimity+rsi8+ctx | −0.181 | −0.027 |

**Finding:** no intraday confluence is positive in both slices. SHORT and MEDIUM
are declared **not viable on price-only data**.

## 8. LONG (H4) deep-dive — the one survivor

H4 with trend filter, stop 2.0×ATR / target 2.0×ATR, **one-active-trade lifecycle**
(matching the real `TradeLifecycleManager`):

| Year | n | win | expR | totalR | maxDD (R) |
|---|---|---|---|---|---|
| 2021 | 48 | 0.396 | +0.133 | +6.4 | 6.9 |
| 2022 | 63 | 0.381 | +0.084 | +5.3 | 6.6 |
| 2023 | 66 | 0.379 | +0.101 | +6.7 | 8.0 |
| 2024 | 61 | 0.344 | **−0.012** | −0.7 | 10.3 |
| 2025 | 63 | 0.381 | +0.084 | +5.3 | 13.4 |
| **Pooled** | **301** | **0.375** | **+0.076** | **+22.9** | **20.0** |

- Profit factor **1.12**; avg win +1.88R, avg loss −1.01R.
- **Bootstrap 95% CI on expectancy: [−0.090, +0.237] — includes zero.**
- Positive in 4 of 5 years; 2024 flat/negative.

**Two honest caveats:**

1. **Regime asymmetry.** The edge is concentrated in TRENDING_UP
   (n=183, expR **+0.209**) and is **negative** in TRENDING_DOWN
   (n=118, expR **−0.130**). Over 2021–2025 gold trended up, so this is largely
   a long-bias in a bull market, not a symmetric edge. On a sustained gold bear
   leg it would likely lose.
2. **Sample size.** 301 trades over five years is thin; the CI crossing zero
   means the result is *promising but not conclusive*.

Geometry is stable across the surface (stop 1.5–3.0 × target 1.5–3.0 all ≥ 0 on
OOS, better at wider targets), which is a mild positive — the result is not a
single knife-edge parameter choice.

## 9. Verdict per engine

| Engine | Entry | Verdict | Basis |
|---|---|---|---|
| **LONG** | H4 + trend | **Promising, needs more validation** | +0.076R, PF 1.12, 4/5 yrs positive, but CI crosses zero and edge is long-biased |
| **MEDIUM** | M15 | **Not proven viable** | negative in train and OOS across every filter, geometry, and construction |
| **SHORT** | M5 | **Not viable** | negative in every test; spread is 31% of ATR |

ASTRA CORE itself is **negative-expectancy intraday** — an important finding for
the wider project: the horizon work cannot rescue an entry with no intraday edge.

## 10. What was NOT done (and why)

- **No ML / neural nets.** With 301 (H4) or 0 (intraday) robust samples, any
  model would overfit. Adding one to "look smart" would violate the brief.
- **No intraday rescue by parameter mining.** Every intraday cell was tested; the
  negative result is reported rather than buried under a mined winner.
- **No live/paper trading.** Out of scope; this is offline research on real bars.

## 11. Reproduction

```bash
cd research/ayzr_quant_lab
python3 tests/test_astra_port.py && python3 tests/test_pipeline.py && \
python3 tests/test_backtest.py && python3 tests/test_astra_protection.py
python3 experiments/baseline_astra_core.py
python3 experiments/hypothesis_sweep.py
python3 experiments/geometry_grid.py
python3 experiments/alt_hypotheses.py
python3 experiments/final_validation.py
```

Every figure in this report is regenerated by those commands into `reports/*.json`.
