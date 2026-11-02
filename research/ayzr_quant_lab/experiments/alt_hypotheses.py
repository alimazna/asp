#!/usr/bin/env python3
"""Alternative intraday hypotheses — beyond ASTRA's momentum signal.

The sweep and geometry grid proved ASTRA's momentum signal + ATR geometry has no
intraday edge (M5/M15 negative in train AND oos for every cell). Before declaring
the intraday horizons unbuildable, test genuinely different constructions that
still reuse ASTRA's *features* (RSI, regime, structure, ATR) without touching core:

  A. MTF alignment  — entry signal must agree with the higher-TF regime trend
  B. Mean reversion — in RANGING/QUIET, fade RSI extremes
  C. Vol expansion  — enter on ATR/range expansion in the signal direction
  D. Regime-only    — trade the regime trend direction, ignore RSI/structure votes

Each is measured train (2021-23) vs OOS (2024-25). Only a construction positive in
BOTH slices is a candidate. Honest negatives are recorded too.

Usage: python3 experiments/alt_hypotheses.py
"""

from __future__ import annotations

import bisect
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
from ayzr_lab.stats import summarize

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


def aligned_ctx(decs, tf: Timeframe, as_of_sec: int):
    """Last CONTEXT decision whose bar has fully closed by as_of_sec (binary search)."""
    if not decs:
        return None
    step = TIMEFRAME_SECONDS[tf]
    # open times are sorted ascending; find last with open + step <= as_of
    lo, hi = 0, len(decs)
    while lo < hi:
        mid = (lo + hi) // 2
        if decs[mid].open_time_sec + step <= as_of_sec:
            lo = mid + 1
        else:
            hi = mid
    return decs[lo - 1] if lo > 0 else None


def trend_dir(d) -> int:
    if d.regime == RegimeType.TRENDING_UP:
        return +1
    if d.regime == RegimeType.TRENDING_DOWN:
        return -1
    return 0


# --------------------------------------------------------------------------
# Signal constructors: (entry decision, context decisions) -> (dir, stop, tgt) or None
# --------------------------------------------------------------------------
def sig_astra(d, ctx):
    if d.direction == SignalDirection.NONE or d.atr <= 0:
        return None
    return d.direction, d.stop, d.target


def sig_mtf(d, ctx, need=1):
    """A: ASTRA signal AND higher-TF trend agrees."""
    if d.direction == SignalDirection.NONE or d.atr <= 0:
        return None
    want = +1 if d.direction == SignalDirection.LONG else -1
    agree = sum(1 for c in ctx.values() if c and trend_dir(c) == want)
    if agree < need:
        return None
    return d.direction, d.stop, d.target


def sig_regime(d, ctx):
    """D: trade the entry-TF regime trend, RSI must not oppose."""
    td = trend_dir(d)
    if td == 0 or d.atr <= 0:
        return None
    if td > 0 and d.rsi < 50:
        return None
    if td < 0 and d.rsi > 50:
        return None
    direction = SignalDirection.LONG if td > 0 else SignalDirection.SHORT
    if direction == SignalDirection.LONG:
        return direction, d.entry_price - 1.5 * d.atr, d.entry_price + 2.5 * d.atr
    return direction, d.entry_price + 1.5 * d.atr, d.entry_price - 2.5 * d.atr


def sig_mr(d, ctx, hi=68.0, lo=32.0):
    """B: fade RSI extremes but only in a non-trending regime."""
    if d.atr <= 0 or d.regime in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN):
        return None
    if d.rsi >= hi:
        return (SignalDirection.SHORT, d.entry_price + 1.5 * d.atr,
                d.entry_price - 2.5 * d.atr)
    if d.rsi <= lo:
        return (SignalDirection.LONG, d.entry_price - 1.5 * d.atr,
                d.entry_price + 2.5 * d.atr)
    return None


def sig_vol(d, ctx):
    """C: range expansion in the signal direction (rangeRatio proxy via last bar)."""
    if d.direction == SignalDirection.NONE or d.atr <= 0:
        return None
    if d.volatility <= 0:
        return None
    # require above-median volatility among trending regimes
    if d.regime not in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN):
        return None
    return d.direction, d.stop, d.target


CONSTRUCTORS = {
    "astra_raw": (sig_astra, ()),
    "mtf_align1": (sig_mtf, ()),
    "regime_trend": (sig_regime, ()),
    "mean_revert": (sig_mr, ()),
    "vol_trend": (sig_vol, ()),
}


def run(entry_tf: Timeframe, context_tfs, ctor) -> dict:
    tr, oo = [], []
    for y in sorted(TRAIN | OOS):
        bars, decs = get(y, entry_tf)
        ctx_decisions = {}
        for ct in context_tfs:
            ctx_decisions[ct.value] = get(y, ct)[1]
        params = TradeParams(spread=median_spread(y), slippage=0.05, max_bars=500,
                             ambiguous_policy=AmbiguousPolicy.WORST_CASE)
        for d in decs:
            ctx = {}
            for ct in context_tfs:
                ctx[ct.value] = aligned_ctx(ctx_decisions[ct.value], ct, d.open_time_sec)
            out = ctor(d, ctx)
            if out is None:
                continue
            direction, stop, target = out
            t = simulate_trade(direction, d.bar_index, bars, stop, target, params)
            if t:
                t.context = {"year": y}
                (tr if y in TRAIN else oo).append(t)
    st, so = summarize(tr), summarize(oo)
    return {"train_n": st.n, "train_expR": round(st.expectancy_r, 4),
            "train_win": round(st.win_rate, 3),
            "oos_n": so.n, "oos_expR": round(so.expectancy_r, 4),
            "oos_win": round(so.win_rate, 3),
            "oos_pf": round(so.profit_factor, 3) if so.profit_factor not in (float("inf"), 0) else None}


def main() -> int:
    out = {}
    print(f"{'entry':5s} {'ctor':14s} {'train_n':>8s} {'train_expR':>11s} {'oos_n':>7s} {'oos_expR':>10s} {'oos_win':>8s}")
    for tfv, ctxs in (("M5", [Timeframe.M15, Timeframe.H1]),
                      ("M15", [Timeframe.H1, Timeframe.H4]),
                      ("H4", [Timeframe.D1])):
        tf = Timeframe(tfv)
        for cname, (ctor, _) in CONSTRUCTORS.items():
            r = run(tf, ctxs, ctor)
            out[f"{tfv}|{cname}"] = r
            flag = "  <== ROBUST" if r["train_expR"] > 0 and r["oos_expR"] > 0 and r["oos_n"] >= 100 else ""
            print(f"{tfv:5s} {cname:14s} {r['train_n']:8d} {r['train_expR']:+11.4f} "
                  f"{r['oos_n']:7d} {r['oos_expR']:+10.4f} {r['oos_win']:8.3f}{flag}")

    dest = os.path.join(LAB, "reports", "alt_hypotheses.json")
    with open(dest, "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
