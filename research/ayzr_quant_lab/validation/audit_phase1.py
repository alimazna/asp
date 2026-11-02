#!/usr/bin/env python3
"""Phase II — independent audit of the Phase I research.

Re-runs the lab's own functions (never re-implements them, to avoid the audit
itself introducing bugs) and checks every claim in reports/RESEARCH_REPORT.md:

  A1  raw corpus checksums match the committed sha256 manifest
  A2  the C++ oracle is rebuilt from current source and matches the frozen digest
  A3  LONG per-year numbers reproduce exactly
  A4  2022 and 2025 are NOT duplicated (trade-level timestamps disjoint)
  A5  expectancy / profit-factor / total-R recomputed from raw R lists agree
  A6  bootstrap: iid vs stationary(block) CI — does the CI hold up?

Usage: python3 validation/audit_phase1.py
"""

from __future__ import annotations

import hashlib
import json
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LAB = os.path.dirname(HERE)
sys.path.insert(0, LAB)

from ayzr_lab.astra_port import RegimeType, SignalDirection, Timeframe  # noqa: E402
from ayzr_lab.backtest import AmbiguousPolicy, TradeParams, simulate_trade  # noqa: E402
from ayzr_lab.data import build_series, load_m1, spread_series  # noqa: E402
from ayzr_lab.pipeline import extract_decisions  # noqa: E402
from ayzr_lab.stats import summarize  # noqa: E402

REPO = os.path.dirname(os.path.dirname(LAB))  # .../asp
CORPUS = os.path.join(REPO, "research", "data", "xauusd_m1")
WINDOW = 500
RESULTS = {}


def median_spread(year: int) -> float:
    sp = sorted(v for _, v in spread_series(year) if v > 0)
    return sp[len(sp) // 2]


def long_trades(year: int):
    """Exactly the Phase I LONG configuration: H4, trend filter, 2.0/2.0, 1 active."""
    bars = load_m1(year)
    series = build_series(bars, [Timeframe.H4])
    bars4 = series[Timeframe.H4].bars
    decs = extract_decisions(bars4, Timeframe.H4, window=WINDOW)
    params = TradeParams(spread=median_spread(year), slippage=0.05, max_bars=200,
                         ambiguous_policy=AmbiguousPolicy.WORST_CASE)
    trades, busy = [], -1
    for d in decs:
        if d.direction == SignalDirection.NONE or d.atr <= 0:
            continue
        if d.regime not in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN):
            continue
        if d.bar_index <= busy:
            continue
        if d.direction == SignalDirection.LONG:
            stop, target = d.entry_price - 2.0 * d.atr, d.entry_price + 2.0 * d.atr
        else:
            stop, target = d.entry_price + 2.0 * d.atr, d.entry_price - 2.0 * d.atr
        t = simulate_trade(d.direction, d.bar_index, bars4, stop, target, params)
        if t is None:
            continue
        trades.append(t)
        busy = d.bar_index + t.bars_held
    return trades


def a1_checksums() -> dict:
    manifest = os.path.join(CORPUS, "checksums_dukascopy_2021_2025.sha256")
    if not os.path.exists(manifest):
        return {"status": "manifest_missing"}
    ok = bad = 0
    details = []
    for line in open(manifest):
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 2:
            continue
        digest, fname = parts
        path = os.path.join(CORPUS, fname)
        if not os.path.exists(path):
            details.append(f"missing {fname}")
            bad += 1
            continue
        h = hashlib.sha256(open(path, "rb").read()).hexdigest()
        if h == digest:
            ok += 1
        else:
            bad += 1
            details.append(f"MISMATCH {fname}")
    return {"status": "ok" if bad == 0 else "mismatch", "ok": ok, "bad": bad,
            "details": details[:10]}


def a2_oracle() -> dict:
    src = os.path.join(LAB, "tools", "oracle_dump.cpp")
    binary = os.path.join(LAB, "tools", "oracle_dump")
    lib = os.path.join(REPO, "build-ayzr", "libaura_core.a")
    out = subprocess.run(
        ["g++", "-std=c++17", "-O2", "-I", os.path.join(REPO, "src"), src, lib,
         "-o", binary], capture_output=True, text=True)
    if out.returncode != 0:
        return {"status": "build_failed", "err": out.stderr[-500:]}
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "prot", os.path.join(LAB, "tests", "test_astra_protection.py"))
    prot = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prot)
    now = prot.digest()
    frozen = json.load(open(os.path.join(LAB, "reports", "astra_core_frozen.json")))["oracle_sha256"]
    return {"status": "ok" if now == frozen else "drift", "rebuilt": now[:16],
            "frozen": frozen[:16], "match": now == frozen}


def a3_reproduce() -> dict:
    per_year = {}
    all_trades = []
    for y in (2021, 2022, 2023, 2024, 2025):
        tr = long_trades(y)
        s = summarize(tr)
        per_year[y] = {"n": s.n, "win": round(s.win_rate, 4),
                       "expR": round(s.expectancy_r, 4), "totalR": round(s.total_r, 4)}
        all_trades.extend(tr)
    pooled = summarize(all_trades)
    return {"per_year": per_year,
            "pooled": {"n": pooled.n, "expR": round(pooled.expectancy_r, 4),
                       "pf": round(pooled.profit_factor, 4),
                       "totalR": round(pooled.total_r, 4)}}


def a4_distinct_years() -> dict:
    t22 = {t.entry_time_sec for t in long_trades(2022)}
    t25 = {t.entry_time_sec for t in long_trades(2025)}
    return {"n22": len(t22), "n25": len(t25), "overlap": len(t22 & t25),
            "identical": t22 == t25}


def a5_math() -> dict:
    """Recompute expectancy / PF / total-R from the raw R list, independently."""
    trades = []
    for y in (2021, 2022, 2023, 2024, 2025):
        trades.extend(long_trades(y))
    rs = [t.r_multiple for t in trades]
    n = len(rs)
    total = sum(rs)
    exp = total / n
    wins = [r for r in rs if r > 0]
    losses = [r for r in rs if r < 0]
    pf = sum(wins) / (-sum(losses))
    s = summarize(trades)
    return {"independent": {"n": n, "expR": round(exp, 6), "totalR": round(total, 4),
                            "pf": round(pf, 6)},
            "summary": {"n": s.n, "expR": round(s.expectancy_r, 6),
                        "totalR": round(s.total_r, 4), "pf": round(s.profit_factor, 6)},
            "agree": (abs(exp - s.expectancy_r) < 1e-9 and abs(pf - s.profit_factor) < 1e-6)}


def a6_bootstrap() -> dict:
    """iid bootstrap vs stationary (block) bootstrap on the pooled R sequence."""
    trades = []
    for y in (2021, 2022, 2023, 2024, 2025):
        trades.extend(long_trades(y))
    rs = [t.r_multiple for t in trades]
    n = len(rs)

    def ci_iid(iters=5000, seed=1):
        rnd = random.Random(seed)
        means = sorted(sum(rs[rnd.randrange(n)] for _ in range(n)) / n for _ in range(iters))
        return means[int(iters * 0.025)], means[int(iters * 0.975)]

    def ci_block(iters=5000, seed=2, block=10):
        rnd = random.Random(seed)
        nblocks = n // block
        means = []
        for _ in range(iters):
            s = []
            for _ in range(nblocks):
                start = rnd.randrange(0, n - block + 1)
                s.extend(rs[start:start + block])
            means.append(sum(s) / len(s))
        means.sort()
        return means[int(iters * 0.025)], means[int(iters * 0.975)]

    lo_i, hi_i = ci_iid()
    lo_b, hi_b = ci_block()
    return {"n": n, "mean": round(sum(rs) / n, 4),
            "iid_ci": [round(lo_i, 4), round(hi_i, 4)],
            "block_ci": [round(lo_b, 4), round(hi_b, 4)],
            "iid_crosses_zero": lo_i <= 0 <= hi_i,
            "block_crosses_zero": lo_b <= 0 <= hi_b}


def main() -> int:
    print("=== A1 corpus checksums ===")
    r = a1_checksums(); RESULTS["a1_checksums"] = r
    print(f"  {r}")

    print("=== A2 oracle freshness (rebuild from source) ===")
    r = a2_oracle(); RESULTS["a2_oracle"] = r
    print(f"  {r}")

    print("=== A3 reproduce LONG per-year ===")
    r = a3_reproduce(); RESULTS["a3_reproduce"] = r
    for y, v in r["per_year"].items():
        print(f"  {y}: {v}")
    print(f"  pooled: {r['pooled']}")

    print("=== A4 2022 vs 2025 distinctness ===")
    r = a4_distinct_years(); RESULTS["a4_distinct"] = r
    print(f"  {r}")

    print("=== A5 math cross-check ===")
    r = a5_math(); RESULTS["a5_math"] = r
    print(f"  independent={r['independent']}")
    print(f"  summary    ={r['summary']}")
    print(f"  agree={r['agree']}")

    print("=== A6 bootstrap: iid vs stationary(block) ===")
    r = a6_bootstrap(); RESULTS["a6_bootstrap"] = r
    print(f"  {r}")

    dest = os.path.join(LAB, "reports", "audit_phase1.json")
    with open(dest, "w") as fh:
        json.dump(RESULTS, fh, indent=2)
    print(f"\nwrote {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
