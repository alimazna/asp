#!/usr/bin/env python3
"""Phase II — engine optimization: does better trade management rescue LONG?

The Phase II audit showed the LONG edge is concentrated (top 10 of 442 trades =
74% of profit) and cost-fragile. This tests whether standard *management* rules —
not new entries — change that:

  baseline     fixed 2.0/2.0 ATR stop/target (current spec)
  breakeven    move stop to entry once +1.0R is reached
  trail_1.5    ATR trailing stop, 1.5 ATR
  time_stop_20 exit at 20 bars if neither level hit
  partial_tp   take half at +1.0R, trail the rest at 1.5 ATR

Every variant is judged on the corrected grid, train vs OOS, and reported with
the same concentration and cost-sensitivity measures so a management rule is only
called an improvement if it helps in BOTH slices and does not just reshuffle
which few trades carry the result.

Usage: python3 experiments/engine_optimization.py
"""

from __future__ import annotations

import json
import os
import sys

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, LAB)

from ayzr_lab.astra_port import RegimeType, SignalDirection, Timeframe
from ayzr_lab.backtest import AmbiguousPolicy, ExitReason, Trade, TradeParams
from ayzr_lab.data import build_series, load_m1, spread_series
from ayzr_lab.pipeline import extract_decisions
from ayzr_lab.stats import summarize

TRAIN = {2021, 2022, 2023}
OOS = {2024, 2025}
WINDOW = 500
_CACHE = {}


def med_spread(y):
    sp = sorted(v for _, v in spread_series(y) if v > 0)
    return sp[len(sp) // 2]


def h4(y):
    if y not in _CACHE:
        bars = build_series(load_m1(y), [Timeframe.H4])[Timeframe.H4].bars
        _CACHE[y] = (bars, extract_decisions(bars, Timeframe.H4, window=WINDOW))
    return _CACHE[y]


def trend(d):
    return d.regime in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN)


def simulate(bars, entry_i, direction, stop, target, params, mode):
    """Bar-by-bar simulation with optional management. Returns r_multiple."""
    entry_i = entry_i + 1
    if entry_i >= len(bars):
        return None
    risk = abs(stop - bars[entry_i].open)
    if risk <= 0:
        return None
    entry = bars[entry_i].open
    # apply entry friction
    if direction == SignalDirection.LONG:
        entry += params.slippage + params.spread / 2
    else:
        entry -= params.slippage + params.spread / 2

    cur_stop, cur_target = stop, target
    be_moved = False
    partial_done = False
    realized = 0.0
    remaining = 1.0
    mfe = mae = 0.0

    for j in range(entry_i + 1, min(len(bars), entry_i + 1 + params.max_bars)):
        b = bars[j]
        hi = b.high - params.spread / 2 if direction == SignalDirection.LONG else b.high + params.spread / 2
        lo = b.low + params.spread / 2 if direction == SignalDirection.LONG else b.low - params.spread / 2
        mfe = max(mfe, (hi - entry) / risk if direction == SignalDirection.LONG else (entry - lo) / risk)
        mae = min(mae, (lo - entry) / risk if direction == SignalDirection.LONG else (entry - hi) / risk)

        # partial take-profit
        if mode == "partial_tp" and not partial_done:
            if (direction == SignalDirection.LONG and hi >= entry + 1.0 * risk) or \
               (direction == SignalDirection.SHORT and lo <= entry - 1.0 * risk):
                realized += 0.5 * 1.0
                remaining = 0.5
                partial_done = True
                if mode == "partial_tp":
                    cur_stop = entry + 0.0 * risk if direction == SignalDirection.LONG else entry - 0.0 * risk

        # breakeven
        if mode in ("breakeven", "partial_tp") and not be_moved:
            if (direction == SignalDirection.LONG and hi >= entry + 1.0 * risk) or \
               (direction == SignalDirection.SHORT and lo <= entry - 1.0 * risk):
                cur_stop = entry
                be_moved = True

        # trailing
        if mode == "trail_1.5":
            a = 1.5 * risk
            if direction == SignalDirection.LONG:
                cur_stop = max(cur_stop, hi - a)
            else:
                cur_stop = min(cur_stop, lo + a)

        # stop hit
        if (direction == SignalDirection.LONG and lo <= cur_stop) or \
           (direction == SignalDirection.SHORT and hi >= cur_stop):
            r = (cur_stop - entry) / risk if direction == SignalDirection.LONG else (entry - cur_stop) / risk
            return realized + remaining * r, j - entry_i, mfe, mae
        # target hit
        if (direction == SignalDirection.LONG and hi >= cur_target) or \
           (direction == SignalDirection.SHORT and lo <= cur_target):
            r = (cur_target - entry) / risk if direction == SignalDirection.LONG else (entry - cur_target) / risk
            return realized + remaining * r, j - entry_i, mfe, mae

    # time exit
    last = bars[min(len(bars) - 1, entry_i + params.max_bars)]
    r = (last.close - entry) / risk if direction == SignalDirection.LONG else (entry - last.close) / risk
    return realized + remaining * r, params.max_bars, mfe, mae


def run(mode, max_bars=200):
    tr, oo = [], []
    for y in sorted(TRAIN | OOS):
        bars, decs = h4(y)
        p = TradeParams(spread=med_spread(y), slippage=0.05, max_bars=max_bars,
                        ambiguous_policy=AmbiguousPolicy.WORST_CASE)
        busy = -1
        for d in decs:
            if not trend(d) or d.atr <= 0 or d.bar_index <= busy:
                continue
            if d.direction == SignalDirection.LONG:
                stop, target = d.entry_price - 2.0 * d.atr, d.entry_price + 2.0 * d.atr
            else:
                stop, target = d.entry_price + 2.0 * d.atr, d.entry_price - 2.0 * d.atr
            res = simulate(bars, d.bar_index, d.direction, stop, target, p, mode)
            if res is None:
                continue
            r, held, mfe, mae = res
            eb = bars[d.bar_index + 1]
            t = Trade(direction=d.direction, entry_time_sec=eb.open_time_sec,
                      entry_price=eb.open, stop=stop, target=target, risk=d.atr * 2.0,
                      exit_time_sec=bars[min(len(bars) - 1, d.bar_index + 1 + held)].open_time_sec, exit_price=0.0,
                      reason=ExitReason.TIMEOUT, r_multiple=r, gross_r_multiple=r,
                      mae_r=mae, mfe_r=mfe, bars_held=held)
            (tr if y in TRAIN else oo).append(t)
            busy = d.bar_index + held
    return tr, oo


MODES = ["baseline", "breakeven", "trail_1.5", "time_stop_20", "partial_tp"]


def main() -> int:
    out = {}
    print(f"{'mode':14s} {'train n':>8s} {'tr expR':>9s} {'oos n':>7s} {'oos expR':>9s} {'top10%':>7s}")
    for mode in MODES:
        mb = 20 if mode == "time_stop_20" else 200
        tr, oo = run(mode, max_bars=mb)
        st, so = summarize(tr), summarize(oo)
        rs = sorted((t.r_multiple for t in tr + oo), reverse=True)
        tot = sum(rs)
        top10 = sum(rs[:max(1, len(rs) // 10)]) / tot if tot else 0
        out[mode] = {"train_n": st.n, "train_expR": round(st.expectancy_r, 4),
                     "oos_n": so.n, "oos_expR": round(so.expectancy_r, 4),
                     "pooled_expR": round((st.expectancy_r * st.n + so.expectancy_r * so.n) / max(1, st.n + so.n), 4),
                     "top10pct_profit_share": round(top10, 3)}
        print(f"{mode:14s} {st.n:8d} {st.expectancy_r:+9.4f} {so.n:7d} {so.expectancy_r:+9.4f} {top10:7.3f}")

    dest = os.path.join(LAB, "reports", "engine_optimization.json")
    with open(dest, "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
