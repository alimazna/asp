#!/usr/bin/env python3
"""Phase II — frozen holdout test on the operator's 2026 MT5 corpus.

The 2026-06-24 .. 2026-10-08 corpus was never used to build or tune any engine.
It is a genuine forward window. This script freezes the LONG engine exactly as
specified in engines/horizon_engines.json (no parameter changes) and evaluates it
once, then reports the verdict without re-tuning.

Because the 2026 corpus has no ASK side, a conservative fixed spread is used and
a sensitivity band is reported.

Usage: python3 validation/holdout_2026.py
"""

from __future__ import annotations

import csv
import datetime as dt
import json
import os
import sys

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, LAB)

from ayzr_lab.astra_port import Bar, RegimeType, SignalDirection, Timeframe
from ayzr_lab.backtest import AmbiguousPolicy, TradeParams
from ayzr_lab.data import align_grid
from ayzr_lab.pipeline import extract_decisions
from ayzr_lab.stats import summarize
from ayzr_lab.trades import Geometry, run_engine

REPO = os.path.dirname(os.path.dirname(LAB))
MT5_CSV = os.path.join(REPO, "research", "data", "xauusd_m1", "xauusd_m1_real.csv")
ENGINES = os.path.join(LAB, "engines", "horizon_engines.json")


def load_mt5(path: str) -> list:
    bars = []
    with open(path) as fh:
        for row in csv.DictReader(fh):
            ts = dt.datetime.strptime(row["timestamp"], "%Y-%m-%d %H:%M:%S")
            sec = int(ts.replace(tzinfo=dt.timezone.utc).timestamp())
            bars.append(Bar(sec, float(row["open"]), float(row["high"]),
                            float(row["low"]), float(row["close"]),
                            float(row["volume"]), Timeframe.M1))
    return bars


def trend_gate(d):
    return d.regime in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN)


def main() -> int:
    spec = json.load(open(ENGINES))["LONG"]
    print(f"frozen LONG spec: {spec}")

    bars = load_mt5(MT5_CSV)
    print(f"loaded {len(bars)} M1 bars: "
          f"{dt.datetime.utcfromtimestamp(bars[0].open_time_sec)} .. "
          f"{dt.datetime.utcfromtimestamp(bars[-1].open_time_sec)}")

    bars4 = align_grid(bars, Timeframe.H4)
    decs = extract_decisions(bars4, Timeframe.H4, window=500)
    print(f"H4 bars: {len(bars4)}, decisions: {len(decs)}")

    out = {"spec": spec, "m1_bars": len(bars), "h4_bars": len(bars4),
           "window": [str(dt.datetime.utcfromtimestamp(bars[0].open_time_sec)),
                      str(dt.datetime.utcfromtimestamp(bars[-1].open_time_sec))]}

    results = {}
    for spread in (0.20, 0.40, 0.80):
        p = TradeParams(spread=spread, slippage=0.10, max_bars=200,
                        ambiguous_policy=AmbiguousPolicy.WORST_CASE)
        geo = Geometry(spec["geometry"]["stop_mult"], spec["geometry"]["target_mult"])
        trades = run_engine(bars4, decs, p, geo, trend_gate, one_active=True)
        s = summarize(trades)
        results[f"spread_{spread}"] = {
            "n": s.n, "win": round(s.win_rate, 3),
            "expR": round(s.expectancy_r, 4) if s.n else None,
            "pf": round(s.profit_factor, 3) if s.n else None,
            "totalR": round(s.total_r, 2),
        }
        print(f"  spread={spread}: n={s.n} win={s.win_rate:.3f} "
              f"expR={s.expectancy_r:+.4f} totalR={s.total_r:+.2f}")
    out["results"] = results

    # direction split on the primary spread
    p = TradeParams(spread=0.40, slippage=0.10, max_bars=200,
                    ambiguous_policy=AmbiguousPolicy.WORST_CASE)
    trades = run_engine(bars4, decs, p, Geometry(spec["geometry"]["stop_mult"], spec["geometry"]["target_mult"]),
                        trend_gate, one_active=True)
    for side in (SignalDirection.LONG, SignalDirection.SHORT):
        sub = [t for t in trades if t.direction == side]
        s = summarize(sub)
        out[f"side_{side.name}"] = {"n": s.n, "expR": round(s.expectancy_r, 4) if s.n else None,
                                    "win": round(s.win_rate, 3) if s.n else None}
    print("side split:", {k: v for k, v in out.items() if k.startswith("side_")})

    dest = os.path.join(LAB, "reports", "holdout_2026.json")
    with open(dest, "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
