#!/usr/bin/env python3
"""Unit tests for the backtester and the data layer.

Checks the properties that make results trustworthy: no lookahead (fill on the
next bar), correct TP/SL ordering and ambiguity handling, spread/slippage applied
on the right side, MAE/MFE bounds, and calendar-correct resampling.

Usage: python3 tests/test_backtest.py
"""

from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LAB = os.path.dirname(HERE)
sys.path.insert(0, LAB)

from ayzr_lab.astra_port import Bar, SignalDirection, Timeframe
from ayzr_lab.backtest import (AmbiguousPolicy, ExitReason, TradeParams,
                               simulate_trade)
from ayzr_lab.data import align_grid

FAILS = []


def check(cond, name):
    if not cond:
        FAILS.append(name)
    print(f"  {'PASS' if cond else 'FAIL'}  {name}")


def mk(ts, o, h, l, c):
    return Bar(ts, o, h, l, c, 1.0, Timeframe.M5)


def test_no_lookahead():
    print("test_no_lookahead")
    # Decision bar index 0 closes at 100; bar 1 opens at 101. Entry must be 101.
    bars = [mk(0, 99, 101, 98, 100), mk(300, 101, 103, 100, 102)]
    t = simulate_trade(SignalDirection.LONG, 0, bars, stop=95, target=120,
                       params=TradeParams(spread=0.0, slippage=0.0, max_bars=10))
    check(t is not None and abs(t.entry_price - 101.0) < 1e-9, "entry fills at NEXT bar open")
    check(t.entry_time_sec == 300, "entry timestamp is the next bar")


def test_stop_before_target():
    print("test_stop_before_target")
    bars = [mk(0, 100, 100, 100, 100), mk(300, 100, 100, 100, 100),
            mk(600, 100, 130, 90, 120)]  # one bar hits both target(125) and stop(95)
    t = simulate_trade(SignalDirection.LONG, 1, bars, stop=95, target=125,
                       params=TradeParams(spread=0.0, slippage=0.0, max_bars=10,
                                          ambiguous_policy=AmbiguousPolicy.WORST_CASE))
    check(t.reason == ExitReason.STOP, "worst-case assumes stop first on ambiguous bar")
    check(t.ambiguous, "ambiguous bar flagged")


def test_optimistic_policy():
    print("test_optimistic_policy")
    bars = [mk(0, 100, 100, 100, 100), mk(300, 100, 100, 100, 100),
            mk(600, 100, 130, 90, 120)]
    t = simulate_trade(SignalDirection.LONG, 1, bars, stop=95, target=125,
                       params=TradeParams(spread=0.0, slippage=0.0, max_bars=10,
                                          ambiguous_policy=AmbiguousPolicy.OPTIMISTIC))
    check(t.reason == ExitReason.TARGET, "optimistic assumes target first")


def test_spread_charged():
    print("test_spread_charged")
    bars = [mk(0, 100, 100, 100, 100), mk(300, 100, 105, 99, 104)]
    t0 = simulate_trade(SignalDirection.LONG, 0, bars, stop=90, target=200,
                        params=TradeParams(spread=0.0, slippage=0.0, max_bars=10))
    t1 = simulate_trade(SignalDirection.LONG, 0, bars, stop=90, target=200,
                        params=TradeParams(spread=0.5, slippage=0.0, max_bars=10))
    check(abs(t1.entry_price - (t0.entry_price + 0.5)) < 1e-9, "LONG pays spread on entry")


def test_mae_mfe_bounds():
    print("test_mae_mfe_bounds")
    bars = [mk(0, 100, 100, 100, 100), mk(300, 100, 100, 100, 100),
            mk(600, 100, 108, 94, 105), mk(900, 105, 112, 104, 111)]
    t = simulate_trade(SignalDirection.LONG, 1, bars, stop=90, target=200,
                       params=TradeParams(spread=0.0, slippage=0.0, max_bars=10))
    check(t.mfe_r >= t.mae_r - 1e-9, "MFE >= MAE sanity")
    check(t.mae_r >= 0.0 and t.mfe_r >= 0.0, "excursions non-negative")


def test_timeout():
    print("test_timeout")
    bars = [mk(0, 100, 100, 100, 100)] + [mk(300 * i, 100, 100.2, 99.8, 100)
                                          for i in range(1, 8)]
    t = simulate_trade(SignalDirection.LONG, 0, bars, stop=50, target=200,
                       params=TradeParams(spread=0.0, slippage=0.0, max_bars=5))
    check(t.reason == ExitReason.TIMEOUT, "times out when neither level hit")


def test_align_grid():
    print("test_align_grid")
    # 11 one-minute bars (0..600s) -> buckets [0,300) and [300,600) are confirmed
    # closed because a later bucket has started.
    m1 = [mk(i * 60, 100 + i, 101 + i, 99 + i, 100.5 + i) for i in range(11)]
    m5 = align_grid(m1, Timeframe.M5)
    check(len(m5) == 2, "11 M1 bars -> 2 confirmed M5 buckets")
    b = m5[0]
    check(b.open == 100.0 and b.close == 104.5, "M5 first open / last close")
    check(b.high == 105.0 and b.low == 99.0, "M5 max high / min low")
    check(b.open_time_sec == 0, "M5 bucket aligned to grid")


def test_no_partial_bucket():
    print("test_no_partial_bucket")
    # 7 bars: bucket [0,300) is confirmed by bars at 300+; the trailing bucket
    # [300,600) is still forming and must NOT be emitted as closed.
    m1 = [mk(i * 60, 100, 101, 99, 100) for i in range(7)]
    m5 = align_grid(m1, Timeframe.M5)
    check(len(m5) == 1, "partial trailing bucket is not emitted as closed")


def main() -> int:
    for fn in (test_no_lookahead, test_stop_before_target, test_optimistic_policy,
               test_spread_charged, test_mae_mfe_bounds, test_timeout,
               test_align_grid, test_no_partial_bucket):
        fn()
    print(f"\n{'ALL PASS' if not FAILS else 'FAILURES: ' + ', '.join(FAILS)}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    raise SystemExit(main())
