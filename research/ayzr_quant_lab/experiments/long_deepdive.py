#!/usr/bin/env python3
"""Phase II — LONG engine deep-dive on the corrected grid.

Answers the specific questions the brief asks, with reproducible numbers:

  1. Is LONG a genuine edge or a bull-market long bias?  -> direction split
  2. Does it work in down / ranging / high-vol / low-vol? -> regime sub-types
  3. Is profit concentrated in a few trades?              -> concentration curve
  4. Is it cost-sensitive?                                -> spread x slippage
  5. Does macro context (real yield, dollar) condition it? -> external split

All regime labels use only information available at the decision bar.

Usage: python3 experiments/long_deepdive.py
"""

from __future__ import annotations

import datetime as dt
import json
import os
import sys

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, LAB)

from ayzr_lab.astra_port import RegimeType, SignalDirection, Timeframe
from ayzr_lab.backtest import AmbiguousPolicy, TradeParams
from ayzr_lab.data import build_series, load_m1, spread_series
from ayzr_lab.external import load_context
from ayzr_lab.pipeline import extract_decisions
from ayzr_lab.stats import summarize
from ayzr_lab.trades import Geometry, down_gate, run_engine, trend_gate, up_gate

YEARS = [2021, 2022, 2023, 2024, 2025]
WINDOW = 500
_CACHE = {}


def med_spread(y):
    sp = sorted(v for _, v in spread_series(y) if v > 0)
    return sp[len(sp) // 2]


def h4(y):
    if y not in _CACHE:
        bars = load_m1(y)
        series = build_series(bars, [Timeframe.H4])
        _CACHE[y] = (series[Timeframe.H4].bars,
                     extract_decisions(series[Timeframe.H4].bars, Timeframe.H4, window=WINDOW))
    return _CACHE[y]


def run(gate, geo=Geometry(2.0, 2.0), spread_mult=1.0, slip=0.05, max_bars=200):
    trades = []
    for y in YEARS:
        bars, decs = h4(y)
        p = TradeParams(spread=med_spread(y) * spread_mult, slippage=slip,
                        max_bars=max_bars, ambiguous_policy=AmbiguousPolicy.WORST_CASE)
        trades.extend(run_engine(bars, decs, p, geo, gate))
    return trades


def med_vol(trades):
    vs = sorted(t.context.get("vol", 0) for t in trades if t.context.get("vol"))
    return vs[len(vs) // 2] if vs else 0.0


def main() -> int:
    out = {}
    ctx = load_context()

    # enrich trades with point-in-time macro + vol at entry
    def enrich(trades):
        for t in trades:
            d = dt.datetime.utcfromtimestamp(t.entry_time_sec).date()
            t.context["dgs10"] = ctx.value("DGS10", d)
            t.context["real_yield"] = ctx.value("DFII10", d)
            t.context["dxy"] = ctx.value("DXY", d)
            t.context["dxy_chg5"] = ctx.change("DXY", d, 5)
            t.context["cot_net"] = ctx.value("COT_MM_NET", d)
        return trades

    base = enrich(run(trend_gate))
    s = summarize(base)
    out["baseline"] = {"n": s.n, "win": round(s.win_rate, 4), "expR": round(s.expectancy_r, 4),
                       "pf": round(s.profit_factor, 4), "totalR": round(s.total_r, 3),
                       "ddR": round(s.max_drawdown_r, 2)}
    print("baseline:", out["baseline"])

    # ---- 1. direction split ----
    long_only = [t for t in base if t.direction == SignalDirection.LONG]
    short_only = [t for t in base if t.direction == SignalDirection.SHORT]
    sl, ss = summarize(long_only), summarize(short_only)
    out["direction"] = {
        "BUY": {"n": sl.n, "win": round(sl.win_rate, 3), "expR": round(sl.expectancy_r, 4),
                "totalR": round(sl.total_r, 2)},
        "SELL": {"n": ss.n, "win": round(ss.win_rate, 3), "expR": round(ss.expectancy_r, 4),
                 "totalR": round(ss.total_r, 2)},
    }
    print("direction:", out["direction"])

    # ---- 2. regime sub-types (up/down only, split by vol) ----
    def sub(pred, name):
        t = [x for x in base if pred(x)]
        st = summarize(t)
        return {"n": st.n, "win": round(st.win_rate, 3), "expR": round(st.expectancy_r, 4),
                "totalR": round(st.total_r, 2)}

    out["up_only"] = sub(lambda t: t.context["regime"] == "TRENDING_UP", "up")
    out["down_only"] = sub(lambda t: t.context["regime"] == "TRENDING_DOWN", "down")
    print("up_only:", out["up_only"], " down_only:", out["down_only"])

    # ---- 3. trade concentration ----
    rs = sorted((t.r_multiple for t in base), reverse=True)
    total = sum(rs)
    top5 = sum(rs[:5])
    top10 = sum(rs[:10])
    out["concentration"] = {
        "total_r": round(total, 2), "top5_share": round(top5 / total, 3) if total else None,
        "top10_share": round(top10 / total, 3) if total else None,
        "n_winners": sum(1 for r in rs if r > 0),
    }
    print("concentration:", out["concentration"])

    # ---- 4. cost sensitivity ----
    cost = {}
    for sm in (1.0, 2.0, 3.0, 5.0):
        tr = run(trend_gate, spread_mult=sm)
        st = summarize(tr)
        cost[f"spread_x{sm}"] = {"n": st.n, "expR": round(st.expectancy_r, 4),
                                 "pf": round(st.profit_factor, 3)}
    for slip in (0.0, 0.1, 0.2, 0.5):
        tr = run(trend_gate, slip=slip)
        st = summarize(tr)
        cost[f"slip_{slip}"] = {"n": st.n, "expR": round(st.expectancy_r, 4),
                                "pf": round(st.profit_factor, 3)}
    out["cost_sensitivity"] = cost
    print("cost sensitivity:", json.dumps(cost, indent=0))

    # ---- 5. macro conditioning (real yield regime, dollar trend) ----
    macro = {}
    ry = [t.context["real_yield"] for t in base if t.context["real_yield"] is not None]
    if ry:
        med_ry = sorted(ry)[len(ry) // 2]
        macro["real_yield_median"] = med_ry
        hi = [t for t in base if t.context["real_yield"] is not None and t.context["real_yield"] >= med_ry]
        lo = [t for t in base if t.context["real_yield"] is not None and t.context["real_yield"] < med_ry]
        sh, sl2 = summarize(hi), summarize(lo)
        macro["high_real_yield"] = {"n": sh.n, "expR": round(sh.expectancy_r, 4), "win": round(sh.win_rate, 3)}
        macro["low_real_yield"] = {"n": sl2.n, "expR": round(sl2.expectancy_r, 4), "win": round(sl2.win_rate, 3)}
    dxyc = [t.context["dxy_chg5"] for t in base if t.context["dxy_chg5"] is not None]
    if dxyc:
        falling = [t for t in base if t.context["dxy_chg5"] is not None and t.context["dxy_chg5"] < 0]
        rising = [t for t in base if t.context["dxy_chg5"] is not None and t.context["dxy_chg5"] >= 0]
        sf, sr = summarize(falling), summarize(rising)
        macro["dxy_falling_5d"] = {"n": sf.n, "expR": round(sf.expectancy_r, 4), "win": round(sf.win_rate, 3)}
        macro["dxy_rising_5d"] = {"n": sr.n, "expR": round(sr.expectancy_r, 4), "win": round(sr.win_rate, 3)}
    out["macro_conditioning"] = macro
    print("macro:", json.dumps(macro, indent=0))

    dest = os.path.join(LAB, "reports", "long_deepdive.json")
    with open(dest, "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
