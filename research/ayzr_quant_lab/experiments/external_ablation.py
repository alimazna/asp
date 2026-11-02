#!/usr/bin/env python3
"""Phase II — external-data ablation for each horizon.

Tests whether genuinely independent information (real yields, the dollar, VIX,
COT positioning) adds edge *on top of* the price-only ASTRA signal, and whether
that addition survives out-of-sample. External series are daily and are joined
strictly point-in-time (see ayzr_lab/external.py).

A feature is only useful if it improves BOTH the train slice and the OOS slice,
and the ablation shows it is not redundant with a simpler gate.

Usage: python3 experiments/external_ablation.py
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
from ayzr_lab.pipeline import Decision, extract_decisions
from ayzr_lab.stats import summarize
from ayzr_lab.trades import Geometry, run_engine

TRAIN = {2021, 2022, 2023}
OOS = {2024, 2025}
WINDOW = 500
_CACHE = {}


def med_spread(y):
    sp = sorted(v for _, v in spread_series(y) if v > 0)
    return sp[len(sp) // 2]


def get(y, tf):
    key = (y, tf.value)
    if key not in _CACHE:
        bars = load_m1(y)
        series = build_series(bars, [tf])
        _CACHE[key] = (series[tf].bars, extract_decisions(series[tf].bars, tf, window=WINDOW))
    return _CACHE[key]


def trend(d):
    return d.regime in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN)


def make_gate(ctx, cond):
    """Return a gate(d) that also checks a point-in-time external condition."""
    def gate(d):
        if not trend(d):
            return False
        when = dt.datetime.utcfromtimestamp(d.open_time_sec).date()
        return cond(ctx, when)
    return gate


CONDITIONS = {
    "price_only": lambda ctx, w: True,
    "real_yield_low": lambda ctx, w: (ctx.value("DFII10", w) is not None
                                      and ctx.value("DFII10", w) < 1.5),
    "real_yield_falling": lambda ctx, w: (ctx.change("DFII10", w, 20) is not None
                                          and ctx.change("DFII10", w, 20) < 0),
    "dxy_falling_5d": lambda ctx, w: (ctx.change("DXY", w, 5) is not None
                                      and ctx.change("DXY", w, 5) < 0),
    "dxy_falling_20d": lambda ctx, w: (ctx.change("DXY", w, 20) is not None
                                       and ctx.change("DXY", w, 20) < 0),
    "vix_low": lambda ctx, w: (ctx.value("VIXCLS", w) is not None
                               and ctx.value("VIXCLS", w) < 20),
    "vix_high": lambda ctx, w: (ctx.value("VIXCLS", w) is not None
                                and ctx.value("VIXCLS", w) >= 20),
    "cot_mm_long": lambda ctx, w: (ctx.value("COT_MM_NET", w) is not None
                                   and ctx.value("COT_MM_NET", w) > 0),
}


def run(tf, gate):
    tr, oo = [], []
    for y in sorted(TRAIN | OOS):
        bars, decs = get(y, tf)
        p = TradeParams(spread=med_spread(y), slippage=0.05, max_bars=200,
                        ambiguous_policy=AmbiguousPolicy.WORST_CASE)
        trades = run_engine(bars, decs, p, Geometry(2.0, 2.0), gate)
        (tr if y in TRAIN else oo).extend(trades)
    st, so = summarize(tr), summarize(oo)
    return {"train_n": st.n, "train_expR": round(st.expectancy_r, 4),
            "oos_n": so.n, "oos_expR": round(so.expectancy_r, 4),
            "oos_win": round(so.win_rate, 3)}


def main() -> int:
    ctx = load_context()
    out = {}
    for tfv in ("H4", "M15", "M5"):
        tf = Timeframe(tfv)
        print(f"\n=== {tfv} ===")
        for cname, cond in CONDITIONS.items():
            r = run(tf, make_gate(ctx, cond))
            out[f"{tfv}|{cname}"] = r
            flag = "  ROBUST" if r["train_expR"] > 0 and r["oos_expR"] > 0 and r["oos_n"] >= 100 else ""
            print(f"  {cname:20s} train n={r['train_n']:6d} expR={r['train_expR']:+.4f} "
                  f"| OOS n={r['oos_n']:6d} expR={r['oos_expR']:+.4f}{flag}")

    dest = os.path.join(LAB, "reports", "external_ablation.json")
    with open(dest, "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
