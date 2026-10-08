#!/usr/bin/env python3
"""T26 — real XAUUSD M1 -> frozen FeatureSet harness (Agent-A zone).

Feeds real Dukascopy M1 bars through the REAL C++ AnalyticalFeatureEngine and
emits the frozen ``FeatureSet`` JSON the Python model layer consumes.

This driver does **no feature computation**. It is a data-prep boundary:

    M1 CSV -> calendar aggregation to M1/M5/.../MN1 -> aura_feature_dump
           -> frozen FeatureSet JSON -> validated with parse_feature_set

All feature values come from ``build/aura_feature_dump`` (which links
``src/analysis/features/AnalyticalFeatureEngine.cpp``). Every emitted decision
instant is therefore a genuine C++ engine snapshot; the sample is real-data
evidence, not a synthetic pipeline check (that was T01/T24).

Aggregation is calendar-aligned (UTC): a higher-timeframe bar starts at the
bucket boundary and carries the first open, the max high, the min low, the last
close and the summed tick volume of its M1 members. The dumper excludes any bar
that has not fully closed at the decision instant, so partially formed
higher-timeframe bars are never read (no lookahead).

Usage:
    # real corpus (once T25 has pushed the raw yearly CSVs)
    python3 research/features_real/run_features.py \
        --m1 research/data/xauusd_m1/2025.csv \
        --out research/features_real/sample_2025.json --decisions 500

    # determinism / hermetic self-test (no data files needed)
    python3 research/features_real/run_features.py --synthetic --out /tmp/s.json \
        --decisions 20

Exit codes: 0 ok, 2 usage/config, 3 missing input or dump binary, 4 validation.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timezone

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, REPO_ROOT)

TIMEFRAMES = ("M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1")

_SECONDS = {
    "M1": 60,
    "M5": 5 * 60,
    "M15": 15 * 60,
    "M30": 30 * 60,
    "H1": 60 * 60,
    "H4": 4 * 60 * 60,
    "D1": 24 * 60 * 60,
}
# 1970-01-05 was a Monday: the reference for weekly bucket alignment.
_MONDAY_EPOCH = 345600
_WEEK = 7 * 24 * 60 * 60

DEFAULT_DUMP = os.path.join(REPO_ROOT, "build", "aura_feature_dump")


def bucket_start(sec: int, tf: str) -> int:
    """Calendar-aligned bucket start (UTC) for a bar-open second."""
    if tf in _SECONDS:
        width = _SECONDS[tf]
        return (sec // width) * width
    if tf == "W1":
        return ((sec - _MONDAY_EPOCH) // _WEEK) * _WEEK + _MONDAY_EPOCH
    if tf == "MN1":
        dt = datetime.fromtimestamp(sec, tz=timezone.utc)
        return int(dt.replace(day=1, hour=0, minute=0, second=0,
                              microsecond=0).timestamp())
    raise ValueError(f"unknown timeframe {tf!r}")


def load_m1(paths):
    """Read M1 CSVs. A header may be present on each file."""
    rows = []
    for path in paths:
        started = False
        with open(path, newline="") as handle:
            for raw in handle:
                raw = raw.strip()
                if not raw:
                    continue
                cols = [c.strip() for c in raw.split(",")]
                if not started:
                    try:
                        int(cols[0])
                    except (ValueError, IndexError):
                        continue  # header line
                    started = True
                sec = int(cols[0]) // 1000  # ms -> s
                rows.append((sec, float(cols[1]), float(cols[2]),
                             float(cols[3]), float(cols[4]), int(cols[5])))
    rows.sort(key=lambda r: r[0])
    # Reject duplicate/open-time regressions — silent dedup would fabricate data.
    for i in range(1, len(rows)):
        if rows[i][0] <= rows[i - 1][0]:
            raise SystemExit(f"non-monotonic M1 timestamps at row {i}: "
                             f"{rows[i - 1][0]} >= {rows[i][0]}")
    return rows


def aggregate(m1, tf):
    """Aggregate M1 rows into bars for one timeframe (open-time keyed)."""
    out = {}
    for sec, o, h, l, c, v in m1:
        b = bucket_start(sec, tf)
        cur = out.get(b)
        if cur is None:
            out[b] = [o, h, l, c, v]
        else:
            cur[1] = max(cur[1], h)
            cur[2] = min(cur[2], l)
            cur[3] = c
            cur[4] += v
    return sorted(out.items())


def write_bars(path, bars):
    with open(path, "w", newline="") as handle:
        handle.write("timestamp_ms_utc,open,high,low,close,volume\n")
        for b, (o, h, l, c, v) in bars:
            handle.write(f"{b * 1000},{o},{h},{l},{c},{v}\n")


def synth_m1(n_minutes: int, seed: int = 20210101):
    """Deterministic pseudo-random M1 series (self-test only; not evidence)."""
    rows = []
    price = 1800.0
    start = 1735689600  # 2025-01-01 00:00:00 UTC
    for i in range(n_minutes):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        step = ((seed / 0x7FFFFFFF) - 0.5) * 0.4
        o = price
        c = round(price + step, 3)
        h = round(max(o, c) + abs(step) * 0.3, 3)
        l = round(min(o, c) - abs(step) * 0.3, 3)
        v = 10 + (seed % 90)
        rows.append((start + i * 60, o, h, l, c, v))
        price = c
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description="T26 real-data FeatureSet harness")
    ap.add_argument("--m1", nargs="*", default=[],
                    help="M1 CSV path(s); files are concatenated in time order")
    ap.add_argument("--synthetic", action="store_true",
                    help="generate a deterministic synthetic M1 series (self-test)")
    ap.add_argument("--synthetic-minutes", type=int, default=40000)
    ap.add_argument("--out", required=True, help="output FeatureSet JSON path")
    ap.add_argument("--decisions", type=int, default=500,
                    help="max decision instants to emit (0 = all M15 opens)")
    ap.add_argument("--dump", default=DEFAULT_DUMP, help="aura_feature_dump binary")
    ap.add_argument("--workdir", default=None, help="scratch dir (default: temp)")
    ap.add_argument("--write-bars", default=None,
                    help="optional dir to keep the aggregated <TF>.csv files")
    ap.add_argument("--dump-sample", default=None,
                    help="optional pretty-printed sample of the first sets")
    args = ap.parse_args()

    if args.synthetic:
        m1 = synth_m1(args.synthetic_minutes)
    elif args.m1:
        missing = [p for p in args.m1 if not os.path.isfile(p)]
        if missing:
            print(f"missing M1 input: {missing}", file=sys.stderr)
            return 3
        m1 = load_m1(args.m1)
    else:
        ap.error("provide --m1 or --synthetic")

    if not m1:
        print("no M1 rows loaded", file=sys.stderr)
        return 3

    if not os.path.isfile(args.dump) or not os.access(args.dump, os.X_OK):
        print(f"dump binary missing/not executable: {args.dump}", file=sys.stderr)
        return 3

    workdir = args.workdir or tempfile.mkdtemp(prefix="t26_")
    os.makedirs(workdir, exist_ok=True)

    bars_dir = args.write_bars or workdir
    os.makedirs(bars_dir, exist_ok=True)
    m15_open_secs = []
    for tf in TIMEFRAMES:
        bars = aggregate(m1, tf)
        write_bars(os.path.join(bars_dir, f"{tf}.csv"), bars)
        if tf == "M15":
            m15_open_secs = [b for b, _ in bars]

    # Decision instants = M15 bar opens (features use only closed history).
    decisions = m15_open_secs if args.decisions in (0, None) \
        else m15_open_secs[:args.decisions]
    if not decisions:
        print("no M15 bars to derive decisions from", file=sys.stderr)
        return 3
    dec_path = os.path.join(workdir, "decisions.csv")
    with open(dec_path, "w") as handle:
        handle.write("\n".join(str(d) for d in decisions) + "\n")

    out_abs = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out_abs) or ".", exist_ok=True)
    proc = subprocess.run(
        [args.dump, bars_dir, dec_path, out_abs, ",".join(TIMEFRAMES)],
        capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"aura_feature_dump failed ({proc.returncode}): {proc.stderr}",
              file=sys.stderr)
        return 3

    from src.models.features import parse_feature_set

    with open(out_abs) as handle:
        sets = json.load(handle)
    validated = 0
    for i, raw in enumerate(sets):
        parsed = parse_feature_set(raw)
        if parsed.asOfBarOpenSec != decisions[i]:
            print(f"set[{i}] asOfBarOpenSec {parsed.asOfBarOpenSec} != "
                  f"decision {decisions[i]}", file=sys.stderr)
            return 4
        validated += 1

    if args.dump_sample:
        with open(args.dump_sample, "w") as handle:
            json.dump(sets[:min(3, len(sets))], handle, indent=2)

    n_valid = sum(1 for r in sets if r["valid"])
    print(f"T26: {len(m1)} M1 bars -> {validated} decision sets "
          f"({n_valid} valid) -> {out_abs}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
