#!/usr/bin/env python3
"""Prove ayzr_lab.astra_port reproduces ASTRA CORE exactly.

Feeds identical bar windows to (a) the compiled C++ oracle
(tools/oracle_dump, which links the real ASTRA engines) and (b) the Python
port, then diffs every numeric field. A mismatch means the lab's research math
has drifted from ASTRA and must not be used for decisions.

Usage: python3 tests/test_astra_port.py
Exit: 0 all fields match, 1 mismatch, 2 setup error.
"""

from __future__ import annotations

import json
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LAB = os.path.dirname(HERE)
sys.path.insert(0, LAB)

from ayzr_lab import astra_port as ap  # noqa: E402

ORACLE = os.path.join(LAB, "tools", "oracle_dump")
TOL = 1e-9


def make_window(seed: int, n: int, tf: str, drift: float, vol: float):
    rnd = random.Random(seed)
    bars = []
    price = 1900.0
    t0 = 1609459200
    step = ap.TIMEFRAME_SECONDS[ap.Timeframe(tf)]
    for i in range(n):
        o = price
        move = rnd.gauss(drift, vol)
        c = max(1.0, o + move)
        hi = max(o, c) + abs(rnd.gauss(0, vol * 0.5))
        lo = min(o, c) - abs(rnd.gauss(0, vol * 0.5))
        bars.append(ap.Bar(t0 + i * step, o, hi, lo, c, 1.0, ap.Timeframe(tf)))
        price = c
    return bars


def run_oracle(bars, tf: str) -> dict:
    lines = [f"{len(bars)} {tf}"]
    for b in bars:
        lines.append(f"{b.open_time_sec} {b.open!r} {b.high!r} {b.low!r} {b.close!r} {b.volume!r}")
    inp = "\n".join(lines) + "\n"
    res = subprocess.run([ORACLE], input=inp, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"oracle failed rc={res.returncode}: {res.stderr}")
    return json.loads(res.stdout)


def run_port(bars, tf: str) -> dict:
    d = ap.astra_decide(bars, ap.Timeframe(tf))
    return {
        "features": {
            "valid": d.features.valid, "barCount": d.features.bar_count,
            "asOf": d.features.as_of_bar_open_sec, "lastClose": d.features.last_close,
            "emaFast": d.features.ema_fast, "emaSlow": d.features.ema_slow,
            "trendSlope": d.features.trend_slope, "atr": d.features.atr,
            "volatility": d.features.volatility, "rsi": d.features.rsi,
            "momentum": d.features.momentum, "bodyRatio": d.features.body_ratio,
            "rangeRatio": d.features.range_ratio,
        },
        "structure": {
            "valid": d.structure.valid, "bias": d.structure.bias.value,
            "score": d.structure.structure_score, "swings": len(d.structure.swings),
            "lastSwingHigh": d.structure.last_swing_high,
            "lastSwingLow": d.structure.last_swing_low,
        },
        "regime": {
            "valid": d.regime.valid, "regime": d.regime.regime.value,
            "trendStrength": d.regime.trend_strength,
            "volatilityLevel": d.regime.volatility_level,
        },
        "eligibility": {"decision": d.eligibility.decision.value},
        "signal": {
            "valid": d.signal.valid, "direction": d.signal.direction.value,
            "rawStrength": d.signal.raw_strength,
            "referencePrice": d.signal.reference_price,
            "suggestedStop": d.signal.suggested_stop,
            "suggestedTarget": d.signal.suggested_target,
        },
    }


def cmp_block(name: str, o: dict, p: dict, fails: list) -> int:
    checks = 0
    for k, ov in o.items():
        pv = p.get(k)
        if isinstance(ov, bool) or isinstance(pv, bool):
            ok = bool(ov) == bool(pv)
        elif isinstance(ov, str):
            ok = ov == pv
        else:
            ok = abs(float(ov) - float(pv)) <= TOL * max(1.0, abs(float(ov)))
        checks += 1
        if not ok:
            fails.append(f"{name}.{k}: oracle={ov!r} port={pv!r}")
    return checks


def main() -> int:
    if not os.path.exists(ORACLE):
        print(f"setup error: oracle not built at {ORACLE}")
        return 2

    cases = [
        ("M5", 200, 0.05, 1.2),
        ("M15", 300, -0.04, 1.5),
        ("H1", 400, 0.10, 2.0),
        ("H4", 250, -0.02, 2.5),
        ("M5", 60, 0.0, 0.8),      # near minBars boundary
        ("M15", 54, 0.0, 1.0),     # below minBars -> INCOMPLETE
        ("D1", 200, 0.3, 4.0),
    ]

    total_checks = 0
    all_fails = []
    for tf, n, drift, vol in cases:
        for seed in (1, 2, 3, 7, 11):
            bars = make_window(seed, n, tf, drift, vol)
            o = run_oracle(bars, tf)
            p = run_port(bars, tf)
            for block in ("features", "structure", "regime", "eligibility", "signal"):
                total_checks += cmp_block(f"{tf}/n={n}/s={seed}/{block}",
                                          o[block], p[block], all_fails)

    print(f"fidelity checks: {total_checks}")
    if all_fails:
        print(f"MISMATCHES: {len(all_fails)}")
        for f in all_fails[:20]:
            print("  " + f)
        return 1
    print("PASS — Python port reproduces ASTRA CORE on every field")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
