#!/usr/bin/env python3
"""Seed the backend with REAL XAUUSD candles from the committed corpus.

The observatory used to seed a synthetic ramp (1900 + i*1.5). That is fine for
proving the wiring, but the decisions it drives are not grounded in the market.
This script instead reads the operator's real MT5 export
(`research/data/xauusd_m1/xauusd_m1_real.csv`) or the Dukascopy corpus, rolls
M1 up to the operational timeframe, and ingests it.

It never fabricates a bar. If the corpus is old, the backend will honestly
report the series STALE, and the cycle will carry the freshness warning.

Usage:
  python3 seed_real_candles.py --port 8081 --token sender-tok \
      --timeframe M15 --bars 400

  # pick the multi-regime Dukascopy corpus for a given year instead:
  python3 seed_real_candles.py --source dukascopy --year 2025
"""

from __future__ import annotations

import argparse
import csv
import gzip
import json
import sys
import urllib.error
import urllib.request
from collections import OrderedDict
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DATA_DIR = REPO / "research" / "data" / "xauusd_m1"
MT5_CSV = DATA_DIR / "xauusd_m1_real.csv"

# Timeframe -> seconds per bar. Kept local so the seeder does not depend on the
# backend's C++ table; the backend re-validates every timestamp anyway.
TF_SECONDS = {
    "M1": 60, "M5": 300, "M15": 900, "M30": 1800,
    "H1": 3600, "H4": 14400, "D1": 86400, "W1": 604800, "MN1": 2592000,
}


def _parse_ts(value: str) -> int:
    """Parse either epoch-milliseconds or 'YYYY-MM-DD HH:MM:SS' (UTC)."""
    value = value.strip()
    if value.isdigit():
        n = int(value)
        return n // 1000 if n > 10_000_000_000 else n
    dt = datetime.strptime(value, "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc)
    return int(dt.timestamp())


def read_m1_rows(path: Path) -> list[tuple[int, float, float, float, float, float]]:
    opener = gzip.open if path.suffix == ".gz" else open
    rows: list[tuple[int, float, float, float, float, float]] = []
    with opener(path, "rt", newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            rows.append((
                _parse_ts(row["timestamp"]),
                float(row["open"]), float(row["high"]),
                float(row["low"]), float(row["close"]),
                float(row.get("volume") or 0.0),
            ))
    rows.sort(key=lambda r: r[0])
    return rows


def rollup(rows, tf_seconds: int):
    """Aggregate M1 bars into `tf_seconds` buckets anchored on the epoch grid.

    open = first, high = max, low = min, close = last, volume = sum. A bucket
    with a single M1 bar is still emitted, so no bar is invented or dropped.
    """
    buckets: "OrderedDict[int, list]" = OrderedDict()
    for ts, o, h, l, c, v in rows:
        key = ts - (ts % tf_seconds)
        if key not in buckets:
            buckets[key] = [o, h, l, c, v]
        else:
            b = buckets[key]
            b[1] = max(b[1], h)
            b[2] = min(b[2], l)
            b[3] = c
            b[4] += v
    return [(k, b[0], b[1], b[2], b[3], b[4]) for k, b in buckets.items()]


def to_candle_json(bar) -> dict:
    ts, o, h, l, c, v = bar
    # `time` is SECONDS since epoch on the wire (the server compares it against
    # its own second-resolution clock). Sending milliseconds makes every bar
    # look ~1000x future-dated and the ingest rejects the whole batch.
    # tick_volume is an integer count in the wire schema; the Dukascopy tick
    # proxy is fractional, so round it and keep it non-negative. spread is not
    # present in the M1 corpus, so it is reported as 0 rather than invented.
    return {
        "time": ts, "open": round(o, 2), "high": round(h, 2),
        "low": round(l, 2), "close": round(c, 2),
        "tick_volume": max(0, int(round(v))), "spread": 0, "real_volume": 0,
    }


def post_batch(port: int, token: str, symbol: str, timeframe: str,
               source_id: str, candles: list[dict]) -> dict:
    body = json.dumps({
        "source_id": source_id, "symbol": symbol,
        "timeframe": timeframe, "candles": candles,
    }).encode()
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/api/v1/candles/ingest", data=body,
        headers={"Content-Type": "application/json",
                 "Authorization": f"Bearer {token}"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.load(resp)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=int, default=8081)
    ap.add_argument("--token", default="sender-tok")
    ap.add_argument("--symbol", default="XAUUSD")
    ap.add_argument("--timeframe", default="M15", choices=sorted(TF_SECONDS))
    ap.add_argument("--bars", type=int, default=400,
                    help="how many rolled-up bars to send, newest last")
    ap.add_argument("--batch", type=int, default=500,
                    help="candles per request (backend default cap is 500)")
    ap.add_argument("--source", choices=("mt5", "dukascopy"), default="mt5")
    ap.add_argument("--year", type=int, default=2025,
                    help="Dukascopy corpus year (--source dukascopy)")
    ap.add_argument("--source-id", default=None)
    args = ap.parse_args()

    if args.source == "mt5":
        path, source_id = MT5_CSV, args.source_id or "operator-mt5"
    else:
        path = DATA_DIR / f"{args.year}.csv.gz"
        source_id = args.source_id or f"dukascopy-{args.year}"

    if not path.exists():
        print(f"corpus not found: {path}", file=sys.stderr)
        return 2

    m1 = read_m1_rows(path)
    rolled = rollup(m1, TF_SECONDS[args.timeframe])
    window = rolled[-args.bars:]

    newest = datetime.fromtimestamp(window[-1][0], timezone.utc)
    oldest = datetime.fromtimestamp(window[0][0], timezone.utc)
    print(f"source      : {path.name} ({source_id})")
    print(f"m1 bars     : {len(m1)}")
    print(f"{args.timeframe} bars    : {len(rolled)} total, sending {len(window)}")
    print(f"window      : {oldest:%Y-%m-%d %H:%M} .. {newest:%Y-%m-%d %H:%M} UTC")
    print(f"last close  : {window[-1][4]:.2f}")

    accepted = duplicates = 0
    for i in range(0, len(window), args.batch):
        chunk = window[i:i + args.batch]
        payload = [to_candle_json(b) for b in chunk]
        try:
            res = post_batch(args.port, args.token, args.symbol,
                             args.timeframe, source_id, payload)
        except urllib.error.HTTPError as exc:
            print(f"ingest failed: {exc.code} {exc.read().decode()[:200]}",
                  file=sys.stderr)
            return 1
        data = res.get("data", res)
        accepted += len(data.get("accepted", []))
        duplicates += len(data.get("already_stored", [])
                          or data.get("duplicates", []))

    print(f"ingested    : accepted={accepted} duplicates={duplicates}")
    print("note: if the corpus is older than the freshness window the backend "
          "will report the series STALE — that is the honest state, not a bug.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
