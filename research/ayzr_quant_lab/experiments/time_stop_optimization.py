#!/usr/bin/env python3
"""Phase II — engine optimization via exit management (canonical pipeline).

The LONG edge was concentrated and cost-fragile. This sweeps the one management
knob the canonical engine already supports (the timeout horizon, ``max_bars``)
and reports the honest result: a tighter time-stop improves expectancy, profit
factor, and profit distribution in BOTH slices, but the effect still does not
clear the multiple-testing bar.

Usage: python3 experiments/time_stop_optimization.py
"""

from __future__ import annotations

import json
import os
import sys

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, LAB)

from ayzr_lab import stats2 as S
from ayzr_lab.astra_port import Timeframe
from ayzr_lab.backtest import AmbiguousPolicy, TradeParams
from ayzr_lab.data import build_series, load_m1, spread_series
from ayzr_lab.pipeline import extract_decisions
from ayzr_lab.stats import summarize
from ayzr_lab.trades import Geometry, run_engine, trend_gate

TRAIN = {2021, 2022, 2023}
OOS = {2024, 2025}
YEARS = [2021, 2022, 2023, 2024, 2025]
_CACHE = {}


def med_spread(y):
    sp = sorted(v for _, v in spread_series(y) if v > 0)
    return sp[len(sp) // 2]


def h4(y):
    if y not in _CACHE:
        b = build_series(load_m1(y), [Timeframe.H4])[Timeframe.H4].bars
        _CACHE[y] = (b, extract_decisions(b, Timeframe.H4, window=500))
    return _CACHE[y]


def trades_for(max_bars):
    tr, oo, allt = [], [], []
    for y in YEARS:
        b, d = h4(y)
        p = TradeParams(spread=med_spread(y), slippage=0.05, max_bars=max_bars,
                        ambiguous_policy=AmbiguousPolicy.WORST_CASE)
        t = run_engine(b, d, p, Geometry(2.0, 2.0), trend_gate)
        allt += t
        (tr if y in TRAIN else oo).extend(t)
    return tr, oo, allt


def main() -> int:
    out = {}
    print(f"{'max_bars':>8s} {'train n':>8s} {'tr expR':>9s} {'oos n':>7s} {'oos expR':>9s} {'pooled expR':>11s} {'PF':>5s}")
    for mb in (200, 40, 20, 12, 8):
        tr, oo, allt = trades_for(mb)
        st, so, sa = summarize(tr), summarize(oo), summarize(allt)
        out[mb] = {"train_n": st.n, "train_expR": round(st.expectancy_r, 4),
                   "oos_n": so.n, "oos_expR": round(so.expectancy_r, 4),
                   "pooled_n": sa.n, "pooled_expR": round(sa.expectancy_r, 4),
                   "pooled_pf": round(sa.profit_factor, 3)}
        print(f"{mb:8d} {st.n:8d} {st.expectancy_r:+9.4f} {so.n:7d} {so.expectancy_r:+9.4f} "
              f"{sa.expectancy_r:+11.4f} {sa.profit_factor:5.3f}")

    # rigorous verdict on the best (12)
    tr, oo, allt = trades_for(12)
    rs = [t.r_multiple for t in allt]
    sa = summarize(allt)
    rss = sorted(rs, reverse=True)
    tot = sum(rss)
    out["best_12_verdict"] = {
        "n": sa.n, "expR": round(sa.expectancy_r, 4), "pf": round(sa.profit_factor, 3),
        "top10_profit_share": round(sum(rss[:10]) / tot, 3) if tot else None,
        "block_bootstrap": {k: round(v, 4) for k, v in
                            S.stationary_bootstrap_ci(rs, iters=8000, seed=1, mean_block=10).items()},
        "deflated_sharpe_prob": round(S.deflated_sharpe(rs, n_trials=90)["deflated_sharpe_prob"], 4),
    }
    print("\nbest (max_bars=12) verdict:", json.dumps(out["best_12_verdict"], indent=0))

    dest = os.path.join(LAB, "reports", "time_stop_optimization.json")
    with open(dest, "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
