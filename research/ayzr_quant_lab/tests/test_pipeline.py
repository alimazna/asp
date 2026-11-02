#!/usr/bin/env python3
"""Prove the vectorized pipeline matches the scalar ASTRA reference.

The scalar port is already proven against the C++ oracle (test_astra_port.py).
Here we check that the fast numpy path in pipeline.py produces the same feature
values and the same decisions as astra_port on the SAME rolling windows, so the
corpus-scale sweeps are trustworthy.

Usage: python3 tests/test_pipeline.py
"""

from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LAB = os.path.dirname(HERE)
sys.path.insert(0, LAB)

from ayzr_lab import astra_port as ap          # noqa: E402
from ayzr_lab import pipeline as pl            # noqa: E402
from ayzr_lab.data import load_m1, align_grid  # noqa: E402

TOL = 1e-6
WINDOW = 500


def main() -> int:
    # A real slice of 2021 keeps the test grounded in the actual corpus.
    bars = load_m1(2021)[:4000]
    tf = ap.Timeframe.M5
    m5 = align_grid(bars, tf)
    if len(m5) < 120:
        print("setup error: not enough M5 bars")
        return 2

    fails = []
    checks = 0

    # 1. Feature parity at a set of indices.
    arr = pl.compute_arrays(m5)
    for i in range(60, len(m5), 97):
        window = m5[max(0, i - WINDOW + 1):i + 1]
        f = ap.compute_features(window, tf)
        if not f.valid:
            continue
        for name, got, want in (
            ("ema_fast", arr.ema_fast[i], f.ema_fast),
            ("ema_slow", arr.ema_slow[i], f.ema_slow),
            ("trend_slope", arr.trend_slope[i], f.trend_slope),
            ("atr", arr.atr[i], f.atr),
            ("rsi", arr.rsi[i], f.rsi),
            ("volatility", arr.volatility[i], f.volatility),
            ("momentum", arr.momentum[i], f.momentum),
            ("body_ratio", arr.body_ratio[i], f.body_ratio),
            ("range_ratio", arr.range_ratio[i], f.range_ratio),
        ):
            checks += 1
            if abs(got - want) > TOL * max(1.0, abs(want)):
                fails.append(f"features[{name}]@{i}: pipeline={got} scalar={want}")

    # 2. Decision parity: direction + strength must match on every bar.
    decs = pl.extract_decisions(m5, tf, window=WINDOW)
    by_idx = {d.bar_index: d for d in decs}
    compared = 0
    for i in range(59, len(m5)):
        window = m5[max(0, i - WINDOW + 1):i + 1]
        d_ref = ap.astra_decide(window, tf)
        d_fast = by_idx.get(i)
        if d_fast is None:
            continue
        compared += 1
        checks += 2
        if d_ref.signal.direction.value != d_fast.direction.value:
            fails.append(f"direction@{i}: pipeline={d_fast.direction.value} "
                         f"scalar={d_ref.signal.direction.value}")
        if abs(d_ref.signal.raw_strength - d_fast.raw_strength) > 1e-9:
            fails.append(f"strength@{i}: pipeline={d_fast.raw_strength} "
                         f"scalar={d_ref.signal.raw_strength}")

    print(f"pipeline checks: {checks} (features + {compared} decisions compared)")
    if fails:
        print(f"MISMATCHES: {len(fails)}")
        for f in fails[:20]:
            print("  " + f)
        return 1
    print("PASS — vectorized pipeline matches the scalar ASTRA reference")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
