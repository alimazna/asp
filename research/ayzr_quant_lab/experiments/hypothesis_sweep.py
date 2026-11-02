#!/usr/bin/env python3
"""Hypothesis sweep: which filters actually add edge?

For each entry timeframe (M5/M15/H4) and each single hypothesis, measure the
marginal effect on expectancy across all five years, then split train (2021-2023)
vs out-of-sample (2024-2025). A filter only counts as useful if it improves the
OOS slice too — not just the years it was tuned on.

Usage: python3 experiments/hypothesis_sweep.py
"""

from __future__ import annotations

import json
import os
import sys
import time
from typing import Dict, List

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, LAB)

from ayzr_lab.astra_port import RegimeType, SignalDirection, Timeframe
from ayzr_lab.backtest import AmbiguousPolicy, TradeParams, simulate_trade
from ayzr_lab.data import build_series, load_m1, spread_series
from ayzr_lab.pipeline import Decision, extract_decisions
from ayzr_lab.stats import summarize

TRAIN = {2021, 2022, 2023}
OOS = {2024, 2025}
WINDOW = 500


def median_spread(year: int) -> float:
    sp = sorted(v for _, v in spread_series(year) if v > 0)
    return sp[len(sp) // 2] if sp else 0.20


def hours(sec: int) -> int:
    return (sec // 3600) % 24


# ---- hypotheses (each: name, predicate over a Decision) --------------------
HYPOTHESES = {
    "baseline": lambda d: True,
    "trend_only": lambda d: d.regime in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN),
    "min_strength_1.0": lambda d: d.raw_strength >= 0.99,
    "rsi_extreme_8": lambda d: abs(d.rsi - 50.0) >= 8.0,
    "rsi_extreme_12": lambda d: abs(d.rsi - 50.0) >= 12.0,
    "session_london_ny": lambda d: hours(d.open_time_sec) in list(range(7, 21)),
    "no_volatile": lambda d: d.regime != RegimeType.VOLATILE,
    "trend_and_rsi8": lambda d: (d.regime in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN)
                                 and abs(d.rsi - 50.0) >= 8.0),
    "trend_rsi8_session": lambda d: (d.regime in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN)
                                     and abs(d.rsi - 50.0) >= 8.0
                                     and hours(d.open_time_sec) in list(range(7, 21))),
    "vol_band_mid": lambda d: d.entry_price > 0 and 0.0004 <= (d.atr / d.entry_price) <= 0.004,
}


def collect(year: int, tf: Timeframe):
    bars = load_m1(year)
    series = build_series(bars, [tf])
    decisions = extract_decisions(series[tf].bars, tf, window=WINDOW)
    return series[tf].bars, decisions, median_spread(year)


def backtest(decisions: List[Decision], bars, spread: float, year: int,
             pred, stop_mult=None, target_mult=None, max_bars=500):
    params = TradeParams(spread=spread, slippage=0.05, max_bars=max_bars,
                         ambiguous_policy=AmbiguousPolicy.WORST_CASE)
    trades = []
    for d in decisions:
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
        t = simulate_trade(d.direction, d.bar_index, bars, stop, target, params)
        if t is not None:
            t.context = {"regime": d.regime.value, "year": year}
            trades.append(t)
    return trades


def main() -> int:
    results = {}
    for tf in (Timeframe.M5, Timeframe.M15, Timeframe.H4):
        cache = {y: collect(y, tf) for y in sorted(TRAIN | OOS)}
        for hname, pred in HYPOTHESES.items():
            tr, oo = [], []
            for y, (bars, decs, sp) in cache.items():
                trades = backtest(decs, bars, sp, y, pred)
                (tr if y in TRAIN else oo).extend(trades)
            st, so = summarize(tr), summarize(oo)
            results[f"{tf.value}|{hname}"] = {
                "train_n": st.n, "train_expR": round(st.expectancy_r, 4),
                "train_pf": round(st.profit_factor, 3) if st.profit_factor != float("inf") else None,
                "oos_n": so.n, "oos_expR": round(so.expectancy_r, 4),
                "oos_pf": round(so.profit_factor, 3) if so.profit_factor != float("inf") else None,
                "oos_win": round(so.win_rate, 3),
            }
            print(f"{tf.value:4s} {hname:22s} train n={st.n:6d} expR={st.expectancy_r:+.4f} "
                  f"| oos n={so.n:6d} expR={so.expectancy_r:+.4f} win={so.win_rate:.3f}")

    dest = os.path.join(LAB, "reports", "hypothesis_sweep.json")
    with open(dest, "w") as fh:
        json.dump(results, fh, indent=2)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
