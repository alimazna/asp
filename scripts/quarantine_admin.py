#!/usr/bin/env python3
"""Inspect and purge contaminated candle rows on a deployed ASTRA backend.

The guarded read APIs withhold an entire (symbol, timeframe) series when any of
its rows is misaligned to the timeframe grid. That is deliberate — a synthetic
row must never be served as real data — but it also hides the offending row
from the operator. This tool uses the admin-only inspection view to show the
raw rows and, when asked, deletes a single row by its full identity.

Usage:
    # read the admin credential from the environment (never a CLI argument)
    export ASTRA_ADMIN_TOKEN=...
    python3 scripts/quarantine_admin.py list --tf M15
    python3 scripts/quarantine_admin.py list --all

    # delete one row, identified by the exact identity printed by `list`
    python3 scripts/quarantine_admin.py purge \
        --source-id railway-verify --tf M15 --open-time 1792068271

The admin credential is never logged. Only the base URL and the row identities
are printed.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.request

DEFAULT_BASE = "https://astra-server-production.up.railway.app"
TIMEFRAMES = ["M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"]
SYMBOL = "XAUUSD"

# Grid length in seconds, for the timeframes that have a fixed grid. D1 and
# above are calendar-anchored and are not judged here, matching the server.
GRID_SECONDS = {"M1": 60, "M5": 300, "M15": 900, "M30": 1800, "H1": 3600,
                "H4": 14400}


def call(base: str, token: str, path: str, method: str = "GET",
         body: dict | None = None) -> dict:
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(
        base.rstrip("/") + path, data=data, method=method,
        headers={"Authorization": "Bearer " + token,
                 "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode()[:300]
        sys.exit(f"HTTP {exc.code} from {path}: {detail}")


def list_rows(base: str, token: str, timeframe: str) -> None:
    payload = call(base, token, f"/api/v1/admin/candles?tf={timeframe}&limit=1000")
    rows = payload["data"]["rows"]
    grid = GRID_SECONDS.get(timeframe)
    print(f"{timeframe}: {len(rows)} row(s)")
    for row in rows:
        open_time = row["open_time"]
        flag = ""
        if grid is not None and open_time % grid != 0:
            flag = f"  <-- MISALIGNED (open_time % {grid} = {open_time % grid})"
        print(f"  source_id={row['source_id']!r} open_time={open_time}{flag}")
        if flag:
            print(f"    purge: --source-id {row['source_id']!r} "
                  f"--tf {timeframe} --open-time {open_time}")


def purge_row(base: str, token: str, source_id: str, timeframe: str,
              open_time: int) -> None:
    body = {"source_id": source_id, "symbol": SYMBOL, "timeframe": timeframe,
            "open_time": open_time}
    payload = call(base, token, "/api/v1/admin/candles", method="DELETE",
                   body=body)
    data = payload["data"]
    print(f"{data['state']}: deleted={data['deleted']} "
          f"({source_id} {timeframe} {open_time})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default=os.environ.get("ASTRA_BASE", DEFAULT_BASE))
    sub = parser.add_subparsers(dest="command", required=True)

    list_cmd = sub.add_parser("list", help="show raw rows, flagging misaligned ones")
    list_cmd.add_argument("--tf", choices=TIMEFRAMES)
    list_cmd.add_argument("--all", action="store_true",
                          help="every supported timeframe")

    purge_cmd = sub.add_parser("purge", help="delete exactly one row by identity")
    purge_cmd.add_argument("--source-id", required=True)
    purge_cmd.add_argument("--tf", required=True, choices=TIMEFRAMES)
    purge_cmd.add_argument("--open-time", required=True, type=int)

    args = parser.parse_args()

    token = os.environ.get("ASTRA_ADMIN_TOKEN", "")
    if not token:
        sys.exit("ASTRA_ADMIN_TOKEN is not set. Export it; do not pass it on "
                 "the command line.")

    if args.command == "list":
        targets = TIMEFRAMES if args.all else [args.tf or "M15"]
        for timeframe in targets:
            list_rows(args.base, token, timeframe)
        return 0

    purge_row(args.base, token, args.source_id, args.tf, args.open_time)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
