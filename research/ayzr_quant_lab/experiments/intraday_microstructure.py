#!/usr/bin/env python3
"""Phase II — genuinely new intraday hypotheses for SHORT / MEDIUM.

The Phase I finding was that no *ASTRA-signal* filter helps M5/M15. This tests
different, documented intraday mechanisms instead of re-tuning thresholds:

  H-BB   Bollinger squeeze breakout — enter on a close outside the band after a
         volatility contraction (classic volatility-breakout).
  H-VE   Volatility-expansion continuation — enter in the direction of a wide
         range bar that closes near its extreme (momentum ignition).
  H-FB   Failed-breakout reversal — fade a break of the prior swing high/low that
         closes back inside the range (liquidity sweep).
  H-ORB  Opening-range breakout — break of the first-hour range at the London and
         NY opens, in the direction of the break.

All are computed from closed bars only, no lookahead, and are evaluated train vs
OOS. Any that fail in either slice is recorded as a negative result.

Usage: python3 experiments/intraday_microstructure.py
"""

from __future__ import annotations

import json
import os
import sys

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, LAB)

from ayzr_lab.astra_port import Bar, RegimeType, SignalDirection, Timeframe
from ayzr_lab.backtest import AmbiguousPolicy, TradeParams, simulate_trade
from ayzr_lab.data import build_series, load_m1, spread_series
from ayzr_lab.stats import summarize

TRAIN = {2021, 2022, 2023}
OOS = {2024, 2025}
_CACHE = {}


def med_spread(y):
    sp = sorted(v for _, v in spread_series(y) if v > 0)
    return sp[len(sp) // 2]


def bars_of(y, tf):
    key = (y, tf.value)
    if key not in _CACHE:
        b = load_m1(y)
        _CACHE[key] = build_series(b, [tf])[tf].bars
    return _CACHE[key]


def atr_series(bars, period=14):
    out = [None] * len(bars)
    trs = []
    for i, b in enumerate(bars):
        if i == 0:
            tr = b.high - b.low
        else:
            pc = bars[i - 1].close
            tr = max(b.high - b.low, abs(b.high - pc), abs(b.low - pc))
        trs.append(tr)
        if i >= period - 1:
            out[i] = sum(trs[i - period + 1:i + 1]) / period
    return out


def sma(vals, i, period):
    if i < period - 1:
        return None
    return sum(vals[i - period + 1:i + 1]) / period


def stdev(vals, i, period):
    if i < period - 1:
        return None
    w = vals[i - period + 1:i + 1]
    m = sum(w) / period
    return (sum((x - m) ** 2 for x in w) / period) ** 0.5


# --------------------------------------------------------------------------
def gen_signals(bars, kind, params):
    """Yield (bar_index, direction, stop, target) using closed-bar info only."""
    closes = [b.close for b in bars]
    atr = atr_series(bars)
    n = len(bars)
    for i in range(60, n - 1):
        a = atr[i]
        if not a or a <= 0:
            continue
        b = bars[i]
        d = None

        if kind == "BB":
            m = sma(closes, i, params["p"])
            sd = stdev(closes, i, params["p"])
            if m is None or sd is None or sd <= 0:
                continue
            # squeeze: current band width below its own recent average
            widths = []
            for j in range(i - 20, i):
                mj = sma(closes, j, params["p"]); sj = stdev(closes, j, params["p"])
                if mj and sj:
                    widths.append(sj)
            if len(widths) < 15 or sd > sum(widths) / len(widths) * 0.8:
                continue
            up, lo = m + params["k"] * sd, m - params["k"] * sd
            if b.close > up:
                d = (SignalDirection.LONG, b.close - params["stop"] * a, b.close + params["tgt"] * a)
            elif b.close < lo:
                d = (SignalDirection.SHORT, b.close + params["stop"] * a, b.close - params["tgt"] * a)

        elif kind == "VE":
            rng = b.high - b.low
            if rng < params["mult"] * a:
                continue
            pos = (b.close - b.low) / rng if rng else 0.5
            if pos > 0.75:
                d = (SignalDirection.LONG, b.close - params["stop"] * a, b.close + params["tgt"] * a)
            elif pos < 0.25:
                d = (SignalDirection.SHORT, b.close + params["stop"] * a, b.close - params["tgt"] * a)

        elif kind == "FB":
            look = params["look"]
            hh = max(bars[j].high for j in range(i - look, i))
            ll = min(bars[j].low for j in range(i - look, i))
            if b.high > hh and b.close < hh:      # swept high, closed back inside
                d = (SignalDirection.SHORT, b.close + params["stop"] * a, b.close - params["tgt"] * a)
            elif b.low < ll and b.close > ll:     # swept low, closed back inside
                d = (SignalDirection.LONG, b.close - params["stop"] * a, b.close + params["tgt"] * a)

        elif kind == "ORB":
            # first hour of the London (07:00) and NY (13:00) sessions
            hour = (b.open_time_sec // 3600) % 24
            minute = (b.open_time_sec // 60) % 60
            for sess in (7, 13):
                if hour == sess and minute >= 30:
                    rng_hi = max(bars[j].high for j in range(i - params["win"], i) if (bars[j].open_time_sec // 3600) % 24 == sess)
                    rng_lo = min(bars[j].low for j in range(i - params["win"], i) if (bars[j].open_time_sec // 3600) % 24 == sess)
                    if b.close > rng_hi:
                        d = (SignalDirection.LONG, b.close - params["stop"] * a, b.close + params["tgt"] * a)
                    elif b.close < rng_lo:
                        d = (SignalDirection.SHORT, b.close + params["stop"] * a, b.close - params["tgt"] * a)

        if d:
            yield i, d[0], d[1], d[2]


def run(tf, kind, params, max_bars=200):
    tr, oo = [], []
    for y in sorted(TRAIN | OOS):
        bars = bars_of(y, tf)
        p = TradeParams(spread=med_spread(y), slippage=0.05, max_bars=max_bars,
                        ambiguous_policy=AmbiguousPolicy.WORST_CASE)
        busy = -1
        for i, direction, stop, target in gen_signals(bars, kind, params):
            if i <= busy:
                continue
            t = simulate_trade(direction, i, bars, stop, target, p)
            if t:
                (tr if y in TRAIN else oo).append(t)
                busy = i + t.bars_held
    return summarize(tr), summarize(oo)


CONFIGS = {
    "BB_p20k2": ("BB", {"p": 20, "k": 2.0, "stop": 1.5, "tgt": 2.5}),
    "VE_mult2": ("VE", {"mult": 2.0, "stop": 1.5, "tgt": 2.5}),
    "VE_mult3": ("VE", {"mult": 3.0, "stop": 1.5, "tgt": 2.5}),
    "FB_look20": ("FB", {"look": 20, "stop": 1.5, "tgt": 2.5}),
    "FB_look50": ("FB", {"look": 50, "stop": 1.5, "tgt": 2.5}),
    "ORB_w6": ("ORB", {"win": 6, "stop": 1.5, "tgt": 2.5}),
}


def main() -> int:
    out = {}
    for tfv in ("M5", "M15"):
        tf = Timeframe(tfv)
        print(f"\n=== {tfv} ===")
        for name, (kind, params) in CONFIGS.items():
            st, so = run(tf, kind, params)
            out[f"{tfv}|{name}"] = {
                "train_n": st.n, "train_expR": round(st.expectancy_r, 4),
                "oos_n": so.n, "oos_expR": round(so.expectancy_r, 4),
                "oos_win": round(so.win_rate, 3) if so.n else None}
            flag = "  ROBUST" if st.expectancy_r > 0 and so.expectancy_r > 0 and so.n >= 100 else ""
            print(f"  {name:12s} train n={st.n:6d} expR={st.expectancy_r:+.4f} "
                  f"| OOS n={so.n:6d} expR={so.expectancy_r:+.4f}{flag}")

    dest = os.path.join(LAB, "reports", "intraday_microstructure.json")
    with open(dest, "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
