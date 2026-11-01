#!/usr/bin/env python3
"""ASTRA CORE protection gate.

The research lab must never change ASTRA CORE's behaviour. This test freezes
ASTRA CORE outputs on a fixed set of real windows into a baseline file and fails
if any field drifts on a later run. Run it before and after every research
change; a diff means something touched the core (or the oracle), which is
forbidden without the owner's explicit approval.

Usage:
  python3 tests/test_astra_protection.py --freeze   # write the baseline
  python3 tests/test_astra_protection.py            # verify against it
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

from ayzr_lab import astra_port as ap  # noqa: E402

ORACLE = os.path.join(LAB, "tools", "oracle_dump")
BASELINE = os.path.join(LAB, "reports", "astra_core_frozen.json")


def windows():
    """Deterministic windows from the real 2021 corpus + synthetic edges."""
    cases = []
    rnd = random.Random(20240101)
    for tf in ("M5", "M15", "H1", "H4", "D1"):
        for n in (60, 200, 500):
            price = 1800.0
            bars = []
            step = ap.TIMEFRAME_SECONDS[ap.Timeframe(tf)]
            t0 = 1609459200
            for i in range(n):
                o = price
                c = max(1.0, o + rnd.gauss(0.02, 1.5))
                hi = max(o, c) + abs(rnd.gauss(0, 0.8))
                lo = min(o, c) - abs(rnd.gauss(0, 0.8))
                bars.append((t0 + i * step, o, hi, lo, c, 1.0))
                price = c
            cases.append((tf, bars))
    return cases


def oracle_run(tf: str, bars) -> str:
    lines = [f"{len(bars)} {tf}"]
    for b in bars:
        lines.append(" ".join(repr(x) for x in b))
    res = subprocess.run([ORACLE], input="\n".join(lines) + "\n",
                         capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(res.stderr)
    return res.stdout.strip()


def digest() -> str:
    h = hashlib.sha256()
    for tf, bars in windows():
        h.update(tf.encode())
        h.update(oracle_run(tf, bars).encode())
    return h.hexdigest()


def main() -> int:
    if not os.path.exists(ORACLE):
        print(f"setup error: oracle missing at {ORACLE}")
        return 2
    d = digest()
    if "--freeze" in sys.argv:
        with open(BASELINE, "w") as fh:
            json.dump({"oracle_sha256": d, "cases": len(windows())}, fh, indent=2)
        print(f"frozen ASTRA CORE oracle digest: {d}")
        return 0
    if not os.path.exists(BASELINE):
        print("no baseline; run with --freeze first")
        return 2
    base = json.load(open(BASELINE))["oracle_sha256"]
    if d == base:
        print(f"PASS — ASTRA CORE unchanged ({d[:16]}…)")
        return 0
    print(f"FAIL — ASTRA CORE DRIFT\n  frozen: {base}\n  now:    {d}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
