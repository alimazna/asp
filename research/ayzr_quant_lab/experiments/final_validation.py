#!/usr/bin/env python3
"""Final validation: tightest-confluence test (M5/M15) + LONG deep-dive.

Part 1 — Confluence stress: even the tightest simultaneous selectivity
(trend + unanimity + RSI extreme + London/NY session + higher-TF agreement) is
tested for M5 and M15. If no confluence subset is positive in BOTH train and OOS,
the intraday horizons are declared not-viable on price-only data — honestly.

Part 2 — LONG deep-dive: the H4 horizon is run with a realistic ONE-ACTIVE-TRADE
lifecycle (matching the real TradeLifecycleManager), reported per year, with a
bootstrap CI, a stop/target sensitivity surface, and a regime split.

Usage: python3 experiments/final_validation.py
"""

from __future__ import annotations

import json
import os
import sys

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, LAB)

from ayzr_lab.astra_port import (
    RegimeType, SignalDirection, Timeframe, TIMEFRAME_SECONDS,
)
from ayzr_lab.backtest import AmbiguousPolicy, TradeParams, simulate_trade
from ayzr_lab.data import build_series, load_m1, spread_series
from ayzr_lab.pipeline import extract_decisions
from ayzr_lab.stats import bootstrap_expectancy, summarize, summarize_by

TRAIN = {2021, 2022, 2023}
OOS = {2024, 2025}
WINDOW = 500
_CACHE = {}


def median_spread(year: int) -> float:
    sp = sorted(v for _, v in spread_series(year) if v > 0)
    return sp[len(sp) // 2] if sp else 0.20


def get(year: int, tf: Timeframe):
    key = (year, tf.value)
    if key not in _CACHE:
        bars = load_m1(year)
        series = build_series(bars, [tf])
        decs = extract_decisions(series[tf].bars, tf, window=WINDOW)
        _CACHE[key] = (series[tf].bars, decs)
    return _CACHE[key]


def aligned(decs, tf, as_of):
    if not decs:
        return None
    step = TIMEFRAME_SECONDS[tf]
    lo, hi = 0, len(decs)
    while lo < hi:
        mid = (lo + hi) // 2
        if decs[mid].open_time_sec + step <= as_of:
            lo = mid + 1
        else:
            hi = mid
    return decs[lo - 1] if lo > 0 else None


def trend(d):
    return d.regime in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN)


def hours(sec):
    return (sec // 3600) % 24


# --------------------------------------------------------------------------
# Part 1 — confluence
# --------------------------------------------------------------------------
def confluence_run(entry_tf, ctx_tfs, pred, stop_mult, rr):
    tr, oo = [], []
    for y in sorted(TRAIN | OOS):
        bars, decs = get(y, entry_tf)
        ctx = {ct.value: get(y, ct)[1] for ct in ctx_tfs}
        params = TradeParams(spread=median_spread(y), slippage=0.05, max_bars=500,
                             ambiguous_policy=AmbiguousPolicy.WORST_CASE)
        for d in decs:
            if d.direction == SignalDirection.NONE or d.atr <= 0:
                continue
            want = +1 if d.direction == SignalDirection.LONG else -1
            ctxd = {k: aligned(v, Timeframe(k), d.open_time_sec) for k, v in ctx.items()}
            if not pred(d, ctxd, want):
                continue
            if d.direction == SignalDirection.LONG:
                stop = d.entry_price - stop_mult * d.atr
                target = d.entry_price + rr * stop_mult * d.atr
            else:
                stop = d.entry_price + stop_mult * d.atr
                target = d.entry_price - rr * stop_mult * d.atr
            t = simulate_trade(d.direction, d.bar_index, bars, stop, target, params)
            if t:
                t.context = {"year": y}
                (tr if y in TRAIN else oo).append(t)
    return summarize(tr), summarize(oo)


CONFLUENCES = {
    "trend": lambda d, c, w: trend(d),
    "trend+unanimity": lambda d, c, w: trend(d) and d.raw_strength >= 0.99,
    "trend+unanimity+rsi8": lambda d, c, w: trend(d) and d.raw_strength >= 0.99 and abs(d.rsi - 50) >= 8,
    "trend+unanimity+rsi8+session": lambda d, c, w: (trend(d) and d.raw_strength >= 0.99
                                                     and abs(d.rsi - 50) >= 8 and 7 <= hours(d.open_time_sec) <= 20),
    "trend+unanimity+rsi8+ctx": lambda d, c, w: (trend(d) and d.raw_strength >= 0.99
                                                 and abs(d.rsi - 50) >= 8
                                                 and all((x is None or trend_dir(x) in (0, w)) for x in c.values())),
}


def trend_dir(x):
    if x.regime == RegimeType.TRENDING_UP:
        return +1
    if x.regime == RegimeType.TRENDING_DOWN:
        return -1
    return 0


def part1():
    print("=== PART 1: tightest confluence, M5 / M15 (train vs OOS) ===")
    out = {}
    for tfv, ctxs in (("M5", [Timeframe.M15, Timeframe.H1]),
                      ("M15", [Timeframe.H1, Timeframe.H4])):
        tf = Timeframe(tfv)
        for name, pred in CONFLUENCES.items():
            st, so = confluence_run(tf, ctxs, pred, 1.5, 2.5)
            out[f"{tfv}|{name}"] = {"train_n": st.n, "train_expR": round(st.expectancy_r, 4),
                                    "oos_n": so.n, "oos_expR": round(so.expectancy_r, 4),
                                    "oos_win": round(so.win_rate, 3)}
            robust = "ROBUST" if st.expectancy_r > 0 and so.expectancy_r > 0 and so.n >= 100 else "no edge"
            print(f"  {tfv:4s} {name:28s} train n={st.n:6d} expR={st.expectancy_r:+.4f} "
                  f"| OOS n={so.n:6d} expR={so.expectancy_r:+.4f}  {robust}")
    return out


# --------------------------------------------------------------------------
# Part 2 — LONG with one-active-trade lifecycle
# --------------------------------------------------------------------------
def lifecycle_run(tf, stop_mult, rr, max_bars=200):
    """Realistic: at most one open trade at a time (matches TradeLifecycleManager)."""
    per_year = {}
    all_trades = []
    for y in sorted(TRAIN | OOS):
        bars, decs = get(y, tf)
        params = TradeParams(spread=median_spread(y), slippage=0.05, max_bars=max_bars,
                             ambiguous_policy=AmbiguousPolicy.WORST_CASE)
        yt = []
        busy_until = -1
        for d in decs:
            if d.direction == SignalDirection.NONE or d.atr <= 0:
                continue
            if not trend(d):
                continue
            if d.bar_index <= busy_until:
                continue  # a trade is already open
            if d.direction == SignalDirection.LONG:
                stop = d.entry_price - stop_mult * d.atr
                target = d.entry_price + rr * stop_mult * d.atr
            else:
                stop = d.entry_price + stop_mult * d.atr
                target = d.entry_price - rr * stop_mult * d.atr
            t = simulate_trade(d.direction, d.bar_index, bars, stop, target, params)
            if t is None:
                continue
            t.context = {"year": y, "regime": d.regime.value}
            yt.append(t)
            busy_until = d.bar_index + t.bars_held
        per_year[y] = summarize(yt)
        all_trades.extend(yt)
    return per_year, all_trades


def part2():
    print("\n=== PART 2: LONG (H4) with one-active-trade lifecycle ===")
    per_year, trades = lifecycle_run(Timeframe.H4, 2.0, 2.0)
    for y, s in per_year.items():
        print(f"  {y}: n={s.n:4d} win={s.win_rate:.3f} expR={s.expectancy_r:+.4f} "
              f"totalR={s.total_r:+.1f} ddR={s.max_drawdown_r:.1f}")
    pooled = summarize(trades)
    ci = bootstrap_expectancy(trades)
    print(f"  POOLED: n={pooled.n} win={pooled.win_rate:.3f} expR={pooled.expectancy_r:+.4f} "
          f"PF={pooled.profit_factor:.2f} totalR={pooled.total_r:+.1f}")
    if ci:
        print(f"  bootstrap 95% CI on expR: [{ci['ci_low']:+.4f}, {ci['ci_high']:+.4f}]")

    print("\n  stop x target sensitivity (expR by OOS subset):")
    sens = {}
    for sm in (1.5, 2.0, 2.5, 3.0):
        for rr in (1.5, 2.0, 2.5, 3.0):
            py, tr = lifecycle_run(Timeframe.H4, sm, rr)
            oos = [t for t in tr if t.context["year"] in OOS]
            so = summarize(oos)
            sens[f"stop{sm}_rr{rr}"] = {"n": so.n, "oos_expR": round(so.expectancy_r, 4),
                                        "oos_win": round(so.win_rate, 3)}
            print(f"    stop={sm} rr={rr}: n={so.n:4d} oos_expR={so.expectancy_r:+.4f} win={so.win_rate:.3f}")

    print("\n  by regime (pooled):")
    by_reg = summarize_by(trades, "regime")
    for k, s in by_reg.items():
        print(f"    {k:14s} n={s.n:4d} expR={s.expectancy_r:+.4f} win={s.win_rate:.3f}")
    return {"per_year": {str(y): s.as_dict() for y, s in per_year.items()},
            "pooled": pooled.as_dict(),
            "bootstrap": ci, "sensitivity": sens,
            "by_regime": {k: v.as_dict() for k, v in by_reg.items()}}


def main():
    out = {"part1_confluence": part1(), "part2_long": part2()}
    dest = os.path.join(LAB, "reports", "final_validation.json")
    with open(dest, "w") as fh:
        json.dump(out, fh, indent=2, default=str)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
