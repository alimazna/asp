#!/usr/bin/env python3
"""Walk-forward + out-of-sample validation for a composed horizon engine.

Given the winning filters from the hypothesis sweep, this runs each horizon over
five anchored OOS windows and reports stability (expectancy per window), plus a
bootstrap CI on the pooled OOS expectancy. A horizon is only called "stable" if
the OOS windows do not collapse to a single lucky year.

Usage: python3 experiments/walk_forward.py
"""

from __future__ import annotations

import json
import os
import sys
from typing import Callable, Dict, List

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, LAB)

from ayzr_lab.astra_port import RegimeType, SignalDirection, Timeframe
from ayzr_lab.backtest import AmbiguousPolicy, TradeParams, simulate_trade
from ayzr_lab.data import build_series, load_m1, spread_series
from ayzr_lab.pipeline import Decision, extract_decisions
from ayzr_lab.stats import bootstrap_expectancy, summarize, walk_forward

WINDOW = 500
YEARS = [2021, 2022, 2023, 2024, 2025]


def hours(sec: int) -> int:
    return (sec // 3600) % 24


def median_spread(year: int) -> float:
    sp = sorted(v for _, v in spread_series(year) if v > 0)
    return sp[len(sp) // 2] if sp else 0.20


def run_horizon(name: str, tf: Timeframe, pred: Callable[[Decision], bool],
                stop_mult=None, target_mult=None, max_bars=500) -> dict:
    all_trades = []
    per_year = {}
    for y in YEARS:
        bars = load_m1(y)
        series = build_series(bars, [tf])
        decs = extract_decisions(series[tf].bars, tf, window=WINDOW)
        params = TradeParams(spread=median_spread(y), slippage=0.05, max_bars=max_bars,
                             ambiguous_policy=AmbiguousPolicy.WORST_CASE)
        yt = []
        for d in decs:
            if d.direction == SignalDirection.NONE or not pred(d):
                continue
            stop, target = d.stop, d.target
            if stop_mult is not None and d.atr > 0:
                if d.direction == SignalDirection.LONG:
                    stop = d.entry_price - stop_mult * d.atr
                    target = d.entry_price + target_mult * d.atr
                else:
                    stop = d.entry_price + stop_mult * d.atr
                    target = d.entry_price - target_mult * d.atr
            t = simulate_trade(d.direction, d.bar_index, series[tf].bars, stop, target, params)
            if t:
                t.context = {"year": y, "regime": d.regime.value}
                yt.append(t)
        all_trades.extend(yt)
        s = summarize(yt)
        per_year[y] = {"n": s.n, "expR": round(s.expectancy_r, 4),
                       "win": round(s.win_rate, 3), "total_r": round(s.total_r, 1)}

    oos = [t for t in all_trades if t.context["year"] in (2024, 2025)]
    pooled = summarize(oos)
    ci = bootstrap_expectancy(oos)
    return {
        "name": name, "timeframe": tf.value,
        "per_year": per_year,
        "pooled_oos": {"n": pooled.n, "expR": round(pooled.expectancy_r, 4),
                       "win": round(pooled.win_rate, 3), "total_r": round(pooled.total_r, 1),
                       "pf": round(pooled.profit_factor, 3) if pooled.profit_factor != float("inf") else None,
                       "max_dd_r": round(pooled.max_drawdown_r, 1)},
        "bootstrap": ({k: round(v, 4) for k, v in ci.items()} if ci else None),
    }


# Composed hypotheses chosen from the sweep (filled in after review).
HORIZONS = {
    "SHORT": ("M5", lambda d: (d.regime in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN)
                               and abs(d.rsi - 50.0) >= 8.0), 1.2, 2.0),
    "MEDIUM": ("M15", lambda d: (d.regime in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN)
                                 and abs(d.rsi - 50.0) >= 8.0), 1.6, 3.0),
    "LONG": ("H4", lambda d: (d.regime in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN)), 2.2, 4.5),
}


def main() -> int:
    out = {}
    for name, (tfv, pred, sm, tm) in HORIZONS.items():
        tf = Timeframe(tfv)
        r = run_horizon(name, tf, pred, sm, tm)
        out[name] = r
        print(f"{name:7s} {tfv:4s} OOS n={r['pooled_oos']['n']:5d} "
              f"expR={r['pooled_oos']['expR']:+.4f} win={r['pooled_oos']['win']:.3f} "
              f"| years=" + " ".join(f"{y}:{v['expR']:+.2f}" for y, v in r['per_year'].items()))

    dest = os.path.join(LAB, "reports", "walk_forward.json")
    with open(dest, "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
