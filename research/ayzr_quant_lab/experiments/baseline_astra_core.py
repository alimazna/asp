#!/usr/bin/env python3
"""ASTRA CORE baseline over the real corpus — the protected reference.

Runs the unmodified ASTRA CORE decision on M15 and H4 for 2021..2025 and reports
honest performance. This is the yardstick every AYZR horizon is compared to; the
numbers here must not be gamed.

Usage: python3 experiments/baseline_astra_core.py [year]
"""

from __future__ import annotations

import json
import os
import sys
import time

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, LAB)

from ayzr_lab.astra_port import Timeframe
from ayzr_lab.runner import astra_core_engine, run_engine
from ayzr_lab.stats import summarize_by


def main() -> int:
    years = [int(sys.argv[1])] if len(sys.argv) > 1 else [2021, 2022, 2023, 2024, 2025]
    out = {}
    for year in years:
        for tf in (Timeframe.M15, Timeframe.H4):
            t0 = time.time()
            res = run_engine(f"astra_core_{tf.value}", year, tf, astra_core_engine,
                             max_bars=500)
            dt = time.time() - t0
            s = res.summary
            out[f"{year}_{tf.value}"] = {
                "n": s.n, "win_rate": round(s.win_rate, 4),
                "expectancy_r": round(s.expectancy_r, 4),
                "profit_factor": round(s.profit_factor, 3) if s.profit_factor != float("inf") else None,
                "total_r": round(s.total_r, 2), "max_dd_r": round(s.max_drawdown_r, 2),
                "avg_bars_held": round(s.avg_bars_held, 1),
                "avg_mae_r": round(s.avg_mae_r, 3), "avg_mfe_r": round(s.avg_mfe_r, 3),
                "targets": s.targets, "stops": s.stops, "timeouts": s.timeouts,
                "ambiguous": s.ambiguous, "median_spread": round(res.median_spread, 3),
                "elapsed_s": round(dt, 1),
                "by_regime": {k: round(v.expectancy_r, 3) for k, v in
                              summarize_by(res.trades, "regime").items()},
            }
            print(f"{year} {tf.value}: n={s.n} win={s.win_rate:.3f} "
                  f"expR={s.expectancy_r:+.4f} PF={s.profit_factor:.2f} "
                  f"ddR={s.max_drawdown_r:.1f} ({dt:.0f}s)")

    dest = os.path.join(LAB, "reports", "baseline_astra_core.json")
    with open(dest, "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
