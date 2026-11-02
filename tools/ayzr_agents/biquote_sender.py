#!/usr/bin/env python3
"""biquote -> ASTRA candle sender.

Fetches REAL XAUUSD OHLC bars from the public, keyless biquote API
(https://biquote.io) and posts the CLOSED bars to the ASTRA receiving API
using the exact same contract as tools/ayzr_agents/mock_sender.py.

This is a real sender: the prices are live market data, not synthetic. It is
the counterpart to mock_sender.py (which stays synthetic, test-only).

Design:
  * Only closed bars are sent (``isOpen == false``). The in-progress bar is
    never sent, so nothing future-dated can reach the analysis path.
  * First run backfills ``--backfill`` bars (default 100) per timeframe.
    Later runs send only bars whose open_time is newer than the last one this
    sender already delivered for that timeframe (state on disk), so a restart
    or an outage catches up instead of leaving a permanent gap.
  * Timestamps are UTC and posted verbatim; ``clock_offset_seconds`` is 0
    because biquote already stamps in UTC (no broker-clock shift to correct).
  * ``source_id`` defaults to ``biquote`` so its rows never collide with the
    MT5 sender's rows: candle identity is
    (source_id, symbol, timeframe, open_time_utc).
  * ``volume`` from biquote is always 0 for FX/CFD, so it is not sent;
    ``tickVolume`` carries the honest activity count and is sent as
    ``tick_volume``.

Timeframes: biquote exposes 1m,5m,15m,30m,1h,4h,1d = 7 of the 9 ASTRA
timeframes. W1 and MN1 are not offered by biquote and are therefore out of
scope here (MT5 remains their source).

Usage (one shot, against a local server):
    ASTRA_SENDER_TOKEN=dev-token ./build-linux/astra_server \
        --data-dir /tmp/astra-demo --port 8790 &
    python3 tools/ayzr_agents/biquote_sender.py --port 8790 --token dev-token

Usage (continuous, poll every 30s):
    python3 tools/ayzr_agents/biquote_sender.py --port 8790 --token dev-token \
        --loop --interval-seconds 30 --state /tmp/biquote_sender_state.json

Dry run (fetch and show what would be sent, no posting):
    python3 tools/ayzr_agents/biquote_sender.py --dry-run
"""

import argparse
import calendar
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# --- Source (biquote) -------------------------------------------------------

BIQUOTE_BASE = "https://biquote.io"
SYMBOL = "XAUUSD"

# ASTRA timeframe name -> biquote interval parameter. Only the seven biquote
# offers; W1/MN1 are deliberately absent (verified: biquote rejects "1w"/"1mo").
TIMEFRAME_TO_INTERVAL = {
    "M1": "1m",
    "M5": "5m",
    "M15": "15m",
    "M30": "30m",
    "H1": "1h",
    "H4": "4h",
    "D1": "1d",
}

# Send order for one pass: highest timeframe first. The engine runs its decision
# cycle on the operational M15 bar and, at that instant, reads the H4/D1 streams
# already in memory to build the multi-timeframe context. Sending M15 before H4
# (dict order) means the very first decision of a fresh engine sees an empty H4
# stream and fails the core gate. Feeding the structural timeframes first makes
# the context available when the trigger bar arrives.
SEND_ORDER = ["D1", "H4", "H1", "M30", "M15", "M5", "M1"]

# --- Receiving API (ASTRA) --------------------------------------------------

INGEST_PATH = "/api/v1/candles/ingest"
SYNC_PATH = "/api/v1/sync/latest"


def http_json(url, token=None, body=None, timeout=20):
    """GET (body None) or POST a JSON body. Returns (status, parsed_json)."""
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method="POST" if data else "GET")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    if data:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode()
            return resp.status, (json.loads(raw) if raw else {})
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode()
        try:
            return exc.code, (json.loads(raw) if raw else {})
        except json.JSONDecodeError:
            return exc.code, {"raw": raw}


def parse_iso_utc_to_epoch(text):
    """'2026-10-30T07:00:00Z' -> epoch seconds. The stamp is UTC, so the
    conversion is done in UTC (calendar.timegm), never local time."""
    base = text.split(".")[0].rstrip("Z")
    return int(calendar.timegm(time.strptime(base, "%Y-%m-%dT%H:%M:%S")))


def fetch_bars(interval, limit, timeout=25):
    """Fetch OHLC bars from biquote. Returns the 'bars' list (newest first)."""
    qs = urllib.parse.urlencode({"interval": interval, "limit": limit})
    url = f"{BIQUOTE_BASE}/api/{SYMBOL}/ohlc?{qs}"
    status, payload = http_json(url, timeout=timeout)
    if status != 200:
        raise RuntimeError(f"biquote {url} -> HTTP {status}: {payload}")
    if "bars" not in payload:
        raise RuntimeError(f"biquote returned no bars: {payload}")
    return payload["bars"]


def to_astra_candles(bars, timeframe, last_open_time):
    """Convert biquote bars to ASTRA candle objects.

    Keeps only closed bars (isOpen false) whose open_time is strictly newer
    than ``last_open_time`` (0 = send everything). Output is oldest-first and
    each candle carries a stable uid so the server ack can be reconciled.
    """
    out = []
    for bar in sorted(bars, key=lambda b: b["openTime"]):
        if bar.get("isOpen", False):
            continue
        open_time = parse_iso_utc_to_epoch(bar["openTime"])
        if open_time <= last_open_time:
            continue
        uid = f"{SYMBOL}-{timeframe}-{open_time}"
        out.append({
            "uid": uid,
            "time": open_time,
            "open": float(bar["open"]),
            "high": float(bar["high"]),
            "low": float(bar["low"]),
            "close": float(bar["close"]),
            "tick_volume": int(bar.get("tickVolume", 0)),
        })
    return out


def load_state(path):
    if path and os.path.exists(path):
        with open(path) as fh:
            return json.load(fh)
    return {}


def save_state(path, state):
    if not path:
        return
    tmp = f"{path}.tmp"
    with open(tmp, "w") as fh:
        json.dump(state, fh)
    os.replace(tmp, path)


def normalize_base(base_url):
    """Return the host root so INGEST_PATH is appended exactly once.

    INGEST_PATH already carries the /api/v1 prefix, so a caller-supplied base
    that ends in /api/v1 would otherwise be doubled into
    /api/v1/api/v1/candles/ingest (HTTP 405). Accept either spelling.
    """
    b = base_url.rstrip("/")
    for suffix in ("/api/v1", "/api"):
        if b.endswith(suffix):
            b = b[: -len(suffix)].rstrip("/")
            break
    return b


def mirror_to_supabase(args, source_id, timeframe, candles):
    """Upsert the same closed bars into the Supabase candle archive.

    Independent of the ASTRA POST: a Supabase outage must never stop the feed
    to the engine. Returns (ok, detail). Never raises.

    Idempotent by construction: the table's primary key is
    (source_id, symbol, timeframe, open_time) and the request uses
    `Prefer: resolution=merge-duplicates`, so re-sending a batch updates in
    place instead of duplicating or erroring.
    """
    if not args.supabase_url or not args.supabase_key:
        return True, "disabled"
    now = int(time.time())
    rows = []
    for c in candles:
        rows.append({
            "source_id": source_id,
            "symbol": SYMBOL,
            "timeframe": timeframe,
            "open_time": c["time"],
            "open": c["open"],
            "high": c["high"],
            "low": c["low"],
            "close": c["close"],
            "tick_volume": c.get("tick_volume", 0),
            "spread": c.get("spread", 0),
            "real_volume": c.get("real_volume", 0),
            "received_at": now,
        })
    url = f"{args.supabase_url.rstrip('/')}/rest/v1/{args.supabase_table}"
    req = urllib.request.Request(
        url, data=json.dumps(rows).encode(), method="POST")
    req.add_header("apikey", args.supabase_key)
    req.add_header("Authorization", f"Bearer {args.supabase_key}")
    req.add_header("Content-Type", "application/json")
    # merge-duplicates = upsert on the primary key; return=minimal keeps the
    # response small (no row echo) so a big backfill stays light.
    req.add_header("Prefer", "resolution=merge-duplicates,return=minimal")
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return (200 <= resp.status < 300), f"HTTP {resp.status}"
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode()[:300]
        return False, f"HTTP {exc.code}: {detail}"
    except Exception as exc:  # noqa: BLE001 - archive must never break the feed
        return False, f"{type(exc).__name__}: {exc}"


def send_batch(base_url, token, source_id, timeframe, candles):
    body = {
        "source_id": source_id,
        "symbol": SYMBOL,
        "timeframe": timeframe,
        # biquote timestamps are UTC; no broker-clock shift needs correcting.
        "clock_offset_seconds": 0,
        "candles": candles,
    }
    url = f"{normalize_base(base_url)}{INGEST_PATH}"
    status, payload = http_json(url, token=token, body=body)
    data = payload.get("data", {}) if isinstance(payload, dict) else {}
    return status, data


def run_once(args, state):
    """One pass over all timeframes. Returns True if every timeframe was ok."""
    ok = True
    for timeframe in SEND_ORDER:
        interval = TIMEFRAME_TO_INTERVAL[timeframe]
        last = int(state.get(timeframe, 0))
        # Backfill on first sight of a timeframe; otherwise just the recent
        # window is enough to catch up (idempotent server dedups anyway).
        limit = args.backfill if last == 0 else args.catchup
        try:
            bars = fetch_bars(interval, limit)
        except Exception as exc:  # noqa: BLE001 - report and continue other tfs
            print(f"[{timeframe}] FETCH FAILED: {exc}", file=sys.stderr)
            ok = False
            continue

        candles = to_astra_candles(bars, timeframe, last)
        if not candles:
            print(f"[{timeframe}] no new closed bars")
            continue

        if args.dry_run:
            newest = candles[-1]["time"]
            print(f"[{timeframe}] DRY-RUN would send {len(candles)} bars "
                  f"({candles[0]['time']}..{newest}) "
                  f"last={candles[-1]['close']}")
            state[timeframe] = candles[-1]["time"]
            continue

        status, data = None, {}
        try:
            status, data = send_batch(args.base_url, args.token,
                                      args.source_id, timeframe, candles)
        except Exception as exc:  # noqa: BLE001 - never let one TF kill the pass
            # A transient network error (backend restarting, DNS blip) must not
            # end the process: the next cycle retries. The watermark is not
            # advanced, so nothing is lost.
            print(f"[{timeframe}] SEND FAILED: {type(exc).__name__}: {exc}",
                  file=sys.stderr)
            ok = False
            continue
        ingested = data.get("ingested")
        dupes = len(data.get("duplicates", []))
        rejected = len(data.get("rejected", []))
        print(f"[{timeframe}] HTTP {status} sent={len(candles)} "
              f"ingested={ingested} duplicates={dupes} rejected={rejected}")
        if status != 200 or rejected:
            ok = False
            if rejected:
                print(f"[{timeframe}] rejected detail: "
                      f"{json.dumps(data.get('rejected'))}", file=sys.stderr)
            continue
        # Mirror the accepted bars into the Supabase archive. Deliberately after
        # the engine accepted them and independent of that result: the archive
        # is a best-effort copy, and its failure must not mark the engine feed
        # as failed (the watermark still advances on the engine's proof).
        mirrored, mirror_detail = mirror_to_supabase(
            args, args.source_id, timeframe, candles)
        if args.supabase_url and args.supabase_key:
            tag = "ok" if mirrored else "FAILED"
            print(f"[{timeframe}] supabase {tag} ({mirror_detail})")
        # Advance the watermark only on a clean, accepted batch: a persisted
        # candle is proof; a failed batch must be retried, not skipped.
        state[timeframe] = candles[-1]["time"]
    return ok


def main():
    ap = argparse.ArgumentParser(description="biquote -> ASTRA candle sender")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8790)
    ap.add_argument("--base-url", default=None,
                    help="full receiver base URL (e.g. "
                         "https://host/api/v1). Overrides --host/--port and is "
                         "required for HTTPS: the bare host/port form only "
                         "builds an http:// URL.")
    ap.add_argument("--token", default=os.environ.get("ASTRA_SENDER_TOKEN", ""))
    ap.add_argument("--source-id", default="biquote")
    ap.add_argument("--backfill", type=int, default=500,
                    help="bars per timeframe on first run (default 500: the "
                         "engine needs >=200 closed M15/H4 bars to clear the "
                         "multi-timeframe gate, so 500 gives full context)")
    ap.add_argument("--catchup", type=int, default=500,
                    help="max recent bars fetched on later runs")
    ap.add_argument("--state", default=None,
                    help="path to the watermark state file")
    ap.add_argument("--loop", action="store_true",
                    help="keep running, polling every --interval-seconds")
    ap.add_argument("--interval-seconds", type=int, default=30)
    ap.add_argument("--dry-run", action="store_true",
                    help="fetch and print only; never post")
    ap.add_argument("--allow-remote", action="store_true",
                    help="permit posting to a non-loopback host (e.g. Railway)")
    # --- Supabase archive (optional; disabled when url or key is empty) ------
    ap.add_argument("--supabase-url", default=os.environ.get("SUPABASE_URL", ""),
                    help="Supabase project URL; enables the archive when set "
                         "with --supabase-key")
    ap.add_argument("--supabase-key", default=os.environ.get("SUPABASE_SECRET_KEY", ""),
                    help="Supabase secret key (server-side only; never logged)")
    ap.add_argument("--supabase-table", default=os.environ.get("SUPABASE_TABLE", "candles"),
                    help="target table name (default candles)")
    args = ap.parse_args()

    if not args.base_url:
        args.base_url = f"http://{args.host}:{args.port}"

    if not args.dry_run:
        if not args.token:
            raise SystemExit("no token: pass --token or set ASTRA_SENDER_TOKEN")
        parsed = urllib.parse.urlparse(args.base_url)
        is_loopback = parsed.hostname in ("127.0.0.1", "localhost", "::1")
        if not is_loopback and not args.allow_remote:
            raise SystemExit(
                "refusing to run against a non-loopback host without --allow-remote")

    state = load_state(args.state)
    while True:
        # Belt-and-braces: nothing in a single cycle may terminate the process.
        # The weekend closes the gold market from Friday ~21:00 UTC to Sunday
        # ~22:00 UTC; during that window there are simply no new closed bars,
        # which is not an error. The loop keeps polling and resumes by itself
        # when the market reopens - no operator action, no restart.
        try:
            ok = run_once(args, state)
        except Exception as exc:  # noqa: BLE001 - a cycle must never be fatal
            print(f"[loop] cycle failed, continuing: "
                  f"{type(exc).__name__}: {exc}", file=sys.stderr)
            ok = False
        save_state(args.state, state)
        if not args.loop:
            raise SystemExit(0 if ok else 1)
        time.sleep(max(5, args.interval_seconds))


if __name__ == "__main__":
    main()
