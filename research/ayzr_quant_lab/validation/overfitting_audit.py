#!/usr/bin/env python3
"""Phase II — overfitting / multiple-testing audit on the CORRECTED grid.

Phase I compared ~150 geometry cells and ~15 filter constructions but reported a
plain iid CI, which ignores that the winner was *selected*. This redoes the LONG
verdict with:

  * the fixed align_grid (Phase I ran on the pre-fix grid — see audit_phase1)
  * a stationary (block) bootstrap instead of iid
  * Deflated Sharpe with n_trials = the real number of configurations tried
  * PBO (CSCV) over the full geometry x filter family
  * per-year and regime splits recomputed

Usage: python3 validation/overfitting_audit.py
"""

from __future__ import annotations

import itertools
import json
import os
import sys

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, LAB)

from ayzr_lab import stats2 as S
from ayzr_lab.astra_port import RegimeType, SignalDirection, Timeframe
from ayzr_lab.backtest import AmbiguousPolicy, TradeParams
from ayzr_lab.data import build_series, load_m1, spread_series
from ayzr_lab.pipeline import extract_decisions
from ayzr_lab.stats import summarize
from ayzr_lab.trades import Geometry, down_gate, run_engine, trend_gate, up_gate

YEARS = [2021, 2022, 2023, 2024, 2025]
WINDOW = 500
_CACHE = {}


def med_spread(y):
    sp = sorted(v for _, v in spread_series(y) if v > 0)
    return sp[len(sp) // 2]


def bars_for(y, tf):
    key = (y, tf.value)
    if key not in _CACHE:
        bars = load_m1(y)
        series = build_series(bars, [tf])
        _CACHE[key] = series[tf].bars
    return _CACHE[key]


def decisions_for(y, tf):
    key = ("d", y, tf.value)
    if key not in _CACHE:
        _CACHE[key] = extract_decisions(bars_for(y, tf), tf, window=WINDOW)
    return _CACHE[key]


def run_all(geo, gate, max_bars=200):
    """Pooled trades over all years for one configuration."""
    trades = []
    for y in YEARS:
        p = TradeParams(spread=med_spread(y), slippage=0.05, max_bars=max_bars,
                        ambiguous_policy=AmbiguousPolicy.WORST_CASE)
        trades.extend(run_engine(bars_for(y, Timeframe.H4),
                                 decisions_for(y, Timeframe.H4), p, geo, gate))
    return trades


def main() -> int:
    out = {}

    # ---- 1. Corrected LONG baseline (trend gate, 2.0/2.0) ----
    trades = run_all(Geometry(2.0, 2.0), trend_gate)
    s = summarize(trades)
    rs = [t.r_multiple for t in trades]
    out["corrected_baseline"] = {
        "n": s.n, "win": round(s.win_rate, 4), "expR": round(s.expectancy_r, 4),
        "pf": round(s.profit_factor, 4), "totalR": round(s.total_r, 4),
        "ddR": round(s.max_drawdown_r, 2),
    }
    print("corrected LONG baseline:", out["corrected_baseline"])

    # ---- 2. Bootstrap comparison ----
    iid = S.stationary_bootstrap_ci(rs, iters=8000, seed=1, mean_block=1.0)
    blk = S.stationary_bootstrap_ci(rs, iters=8000, seed=1, mean_block=10.0)
    out["bootstrap"] = {"iid": {k: round(v, 4) for k, v in iid.items()},
                        "block10": {k: round(v, 4) for k, v in blk.items()}}
    print("bootstrap iid :", out["bootstrap"]["iid"])
    print("bootstrap blk :", out["bootstrap"]["block10"])

    # ---- 3. Family: geometry x gate (the real trial count) ----
    stops = [1.0, 1.5, 2.0, 2.5, 3.0]
    targets = [1.0, 1.5, 2.0, 2.5, 3.0, 4.0]
    gates = {"trend": trend_gate, "up": up_gate, "down": down_gate}
    family = []
    trial_runs = []
    for gname, gate in gates.items():
        for sm, tm in itertools.product(stops, targets):
            tr = run_all(Geometry(sm, tm), gate)
            rs_i = [t.r_multiple for t in tr]
            s_i = summarize(tr)
            family.append({"gate": gname, "stop": sm, "target": tm,
                           "n": s_i.n, "expR": round(s_i.expectancy_r, 4),
                           "pf": round(s_i.profit_factor, 3),
                           "totalR": round(s_i.total_r, 2)})
            trial_runs.append(rs_i)
    out["family"] = family
    n_trials = len(family)
    print(f"\nfamily size (trials): {n_trials}")

    # ---- 4. Deflated Sharpe on the corrected baseline ----
    dsr = S.deflated_sharpe(rs, n_trials=n_trials)
    out["deflated_sharpe"] = {k: round(v, 4) for k, v in dsr.items()}
    print("deflated sharpe:", out["deflated_sharpe"])

    # ---- 5. PBO over the family (equal-length truncated series) ----
    L = min(len(r) for r in trial_runs)
    pbo = S.pbo_cscv([r[:L] for r in trial_runs], n_splits=8, max_combos=200)
    out["pbo"] = {k: round(v, 4) for k, v in pbo.items()} if pbo else None
    print("PBO:", out["pbo"])

    # ---- 6. Holm over the family (one-sided t p-values) ----
    pvals = [S.t_stat_pvalue(r) for r in trial_runs]
    reject = S.holm_bonferroni(pvals, alpha=0.05)
    survivors = [family[i] for i in range(n_trials) if reject[i]]
    out["holm_survivors"] = len(survivors)
    out["holm_survivor_examples"] = survivors[:10]
    print(f"holm survivors at alpha=0.05: {len(survivors)} / {n_trials}")
    for x in survivors[:5]:
        print("   ", x)

    # ---- 7. Per-year / per-regime for the corrected baseline ----
    per_year = {}
    for y in YEARS:
        p = TradeParams(spread=med_spread(y), slippage=0.05, max_bars=200,
                        ambiguous_policy=AmbiguousPolicy.WORST_CASE)
        tr = run_engine(bars_for(y, Timeframe.H4), decisions_for(y, Timeframe.H4),
                        p, Geometry(2.0, 2.0), trend_gate)
        sy = summarize(tr)
        per_year[y] = {"n": sy.n, "win": round(sy.win_rate, 3),
                       "expR": round(sy.expectancy_r, 4), "totalR": round(sy.total_r, 2)}
    out["per_year"] = per_year
    print("\nper year:", per_year)

    by_reg = {}
    for reg in ("TRENDING_UP", "TRENDING_DOWN"):
        sub = [t for t in trades if t.context["regime"] == reg]
        if sub:
            sr = summarize(sub)
            by_reg[reg] = {"n": sr.n, "expR": round(sr.expectancy_r, 4),
                           "win": round(sr.win_rate, 3), "totalR": round(sr.total_r, 2)}
    out["by_regime"] = by_reg
    print("by regime:", by_reg)

    dest = os.path.join(LAB, "reports", "overfitting_audit.json")
    with open(dest, "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
