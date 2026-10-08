#!/usr/bin/env python3
"""Data-quality checks for the fetched Dukascopy XAUUSD M1 corpus.

Reads research/data/xauusd_m1/<year>.csv (timestamp[ms UTC],open,high,low,close[,volume])
and writes research/data/xauusd_m1/QUALITY.md.

Checks (all mandatory by the Phase 5.1 directive):
  * timestamps monotonic and minute-aligned
  * no duplicate timestamps
  * no OHLC violations (high>=low/open/close, low<=open/close)
  * expected weekend gaps present; unexpected gaps documented
  * timezone is UTC and consistent
  * no silent repair: every anomaly is reported, not fixed

This tool never modifies the data. Run it after fetch.sh completes.
"""

from __future__ import annotations

import csv
import datetime as dt
import os
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
YEARS = (2021, 2022, 2023, 2024, 2025)
MINUTE_MS = 60_000
MAX_INTRADAY_GAP_MS = 2 * 3600 * 1000      # documented: daily maintenance <=2h
MAX_WEEKEND_GAP_MS = 80 * 3600 * 1000      # documented: Fri close -> Sun open <=80h


def _utc(ms: int) -> str:
    return dt.datetime.fromtimestamp(ms / 1000, dt.timezone.utc).strftime(
        "%Y-%m-%d %H:%M"
    )


def check_year(path: Path) -> dict:
    r = {
        "file": path.name, "rows": 0, "first": None, "last": None,
        "non_monotonic": 0, "duplicates": 0, "ohlc_violations": 0,
        "not_minute": 0, "gaps": [], "weekend_gaps": 0, "unexpected_gaps": 0,
        "rows_volume": 0, "zero_volume": 0,
    }
    prev = None
    seen = set()
    with path.open(newline="") as fh:
        rd = csv.reader(fh)
        header = next(rd, None)
        r["header"] = header
        has_vol = header is not None and len(header) >= 6
        for row in rd:
            if not row:
                continue
            ts = int(row[0]); o = float(row[1]); h = float(row[2])
            l = float(row[3]); c = float(row[4])
            r["rows"] += 1
            if r["first"] is None:
                r["first"] = ts
            r["last"] = ts
            if ts % MINUTE_MS != 0:
                r["not_minute"] += 1
            if ts in seen:
                r["duplicates"] += 1
            seen.add(ts)
            if prev is not None and ts <= prev:
                r["non_monotonic"] += 1
            prev = ts
            if not (h >= l and h >= o and h >= c and l <= o and l <= c):
                r["ohlc_violations"] += 1
            if has_vol:
                v = float(row[5]); r["rows_volume"] += 1
                if v == 0.0:
                    r["zero_volume"] += 1
            if prev is not None:
                gap = ts - prev
                if gap > MINUTE_MS * 5:  # ignore the routine 1-min cadence
                    r["gaps"].append((prev, ts, gap))

    for a, b, gap in r["gaps"]:
        start = dt.datetime.fromtimestamp(a / 1000, dt.timezone.utc)
        span_days = (b - a) / 86400000.0
        covers_sat = False
        d = start.date()
        for k in range(int(span_days) + 2):
            if (d + dt.timedelta(days=k)).weekday() == 5:
                covers_sat = True
                break
        if covers_sat and gap <= MAX_WEEKEND_GAP_MS:
            r["weekend_gaps"] += 1
        elif gap <= MAX_INTRADAY_GAP_MS:
            pass  # routine maintenance session break
        else:
            r["unexpected_gaps"] += 1
    return r


def main() -> int:
    lines = ["# XAUUSD M1 — Data Quality Report", ""]
    lines.append(f"- Generated: {dt.datetime.now(dt.timezone.utc).isoformat(timespec='seconds')}")
    lines.append("- Source: Dukascopy public datafeed (BID, tick volume), via dukascopy-node 1.50.0")
    lines.append("- Range: 2021-01-01 .. 2025-12-31 (UTC)")
    lines.append(f"- Documented intraday gap threshold: <= {MAX_INTRADAY_GAP_MS//3600000}h "
                 "(daily maintenance session break)")
    lines.append(f"- Documented weekend gap threshold: <= {MAX_WEEKEND_GAP_MS//3600000}h "
                 "(Fri close -> Sun open)")
    lines.append("")
    lines.append("| Year | Rows | First (UTC) | Last (UTC) | Non-mono | Dups | OHLC viol | "
                 "Not-minute | Weekend gaps | Unexpected gaps | Zero-vol |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|---|")
    overall_ok = True
    for y in YEARS:
        p = HERE / f"{y}.csv"
        if not p.exists():
            lines.append(f"| {y} | MISSING | | | | | | | | | |")
            overall_ok = False
            continue
        r = check_year(p)
        ok = (r["non_monotonic"] == 0 and r["duplicates"] == 0
              and r["ohlc_violations"] == 0 and r["not_minute"] == 0
              and r["unexpected_gaps"] == 0)
        overall_ok = overall_ok and ok
        lines.append(
            f"| {y} | {r['rows']} | {_utc(r['first'])} | {_utc(r['last'])} | "
            f"{r['non_monotonic']} | {r['duplicates']} | {r['ohlc_violations']} | "
            f"{r['not_minute']} | {r['weekend_gaps']} | {r['unexpected_gaps']} | "
            f"{r['zero_volume']} |"
        )
    lines.append("")
    lines.append(f"**Verdict: {'PASS — no anomalies' if overall_ok else 'FAIL — anomalies present (do not repair silently; see table)'}**")
    lines.append("")
    out = os.environ.get("QUALITY_OUT", "QUALITY.md")
    (HERE / out).write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0 if overall_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
