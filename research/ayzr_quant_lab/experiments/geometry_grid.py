#!/usr/bin/env python3
"""Exit-geometry research: the highest-leverage lever.

The sweep showed the ASTRA signal is near break-even and frictions tip it
negative intraday. Before blaming the *entry*, this grid tests whether the EXIT
geometry (stop x target multiples of ATR) is the real problem, and whether an
ambiguous-bar policy materially changes the verdict.

For each entry timeframe: sweep stop_mult x target_mult, with and without the
trend filter, and report train vs OOS expectancy. Also report the ambiguous-bar
share and the timeout share, since a geometry that mostly times out is not a
real edge.

Usage: python3 experiments/geometry_grid.py
"""

from __future__ import annotations

import json
import os
import sys

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, LAB)

from ayzr_lab.astra_port import RegimeType, SignalDirection, Timeframe
from ayzr_lab.backtest import AmbiguousPolicy, TradeParams, simulate_trade
from ayzr_lab.data import build_series, load_m1, spread_series
from ayzr_lab.pipeline import extract_decisions
from ayzr_lab.stats import summarize

TRAIN = {2021, 2022, 2023}
OOS = {2024, 2025}
WINDOW = 500
STOPS = [0.5, 1.0, 1.5, 2.0, 3.0]
RRS = [1.0, 1.5, 2.0, 3.0, 4.0]


def median_spread(year: int) -> float:
    sp = sorted(v for _, v in spread_series(year) if v > 0)
    return sp[len(sp) // 2] if sp else 0.20


def trend(d):
    return d.regime in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN)


_CACHE = {}


def load_tf(tf: Timeframe, year: int):
    key = (tf.value, year)
    if key not in _CACHE:
        bars = load_m1(year)
        series = build_series(bars, [tf])
        decs = extract_decisions(series[tf].bars, tf, window=WINDOW)
        _CACHE[key] = (series[tf].bars, decs, median_spread(year))
    return _CACHE[key]


def run(tf: Timeframe, stop_mult: float, rr: float, use_trend: bool,
        policy=AmbiguousPolicy.WORST_CASE):
    tr, oo = [], []
    for y in sorted(TRAIN | OOS):
        bars, decs, spread = load_tf(tf, y)
        params = TradeParams(spread=spread, slippage=0.05, max_bars=500,
                             ambiguous_policy=policy)
        for d in decs:
            if d.direction == SignalDirection.NONE or d.atr <= 0:
                continue
            if use_trend and not trend(d):
                continue
            if d.direction == SignalDirection.LONG:
                stop = d.entry_price - stop_mult * d.atr
                target = d.entry_price + rr * stop_mult * d.atr
            else:
                stop = d.entry_price + stop_mult * d.atr
                target = d.entry_price - rr * stop_mult * d.atr
            t = simulate_trade(d.direction, d.bar_index, bars, stop, target, params)
            if t:
                (tr if y in TRAIN else oo).append(t)
    return summarize(tr), summarize(oo)


def main() -> int:
    out = {}
    for tf in (Timeframe.M5, Timeframe.M15, Timeframe.H4):
        best = None
        for use_trend in (False, True):
            for sm in STOPS:
                for rr in RRS:
                    st, so = run(tf, sm, rr, use_trend)
                    if so.n < 100:
                        continue
                    key = f"{tf.value}|trend={use_trend}|stop={sm}|rr={rr}"
                    out[key] = {
                        "train_n": st.n, "train_expR": round(st.expectancy_r, 4),
                        "oos_n": so.n, "oos_expR": round(so.expectancy_r, 4),
                        "oos_win": round(so.win_rate, 3),
                        "oos_timeout_pct": round(so.timeouts / so.n, 3) if so.n else 0,
                        "oos_ambiguous_pct": round(so.ambiguous / so.n, 3) if so.n else 0,
                    }
                    if best is None or so.expectancy_r > best[1]:
                        best = (key, so.expectancy_r, st.expectancy_r, so.n, so.win_rate)
        if best:
            print(f"{tf.value:4s} BEST: {best[0]}  oos_expR={best[1]:+.4f} "
                  f"train_expR={best[2]:+.4f} n={best[3]} win={best[4]:.3f}")

    # Ambiguous-policy sensitivity at a representative geometry.
    print("\n-- ambiguous policy sensitivity (M15, stop=1.5, rr=2.0) --")
    for pol in (AmbiguousPolicy.WORST_CASE, AmbiguousPolicy.OPTIMISTIC, AmbiguousPolicy.SKIP):
        st, so = run(Timeframe.M15, 1.5, 2.0, True, pol)
        print(f"  {pol.value:12s} train_expR={st.expectancy_r:+.4f} oos_expR={so.expectancy_r:+.4f} "
              f"oos_n={so.n} ambiguous={so.ambiguous}")

    dest = os.path.join(LAB, "reports", "geometry_grid.json")
    with open(dest, "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
