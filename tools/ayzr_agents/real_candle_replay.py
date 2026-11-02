#!/usr/bin/env python3
"""Real XAUUSD M1 candle QA + historical replay against a LOCAL astra_server.

This is the Agent C (backend-hardening) real-data workflow. It is deliberately
evidential: every expected value comes from the genuine MT5 corpus or from an
independently re-derived aggregate. Nothing is invented, no price is synthesised,
and no analysis result is asserted.

Pipeline
--------
1. Verify the corpus sha256 against ``research/data/xauusd_m1/checksums.sha256``
   and refuse to continue on any mismatch (an unverified corpus is not evidence).
2. Load the real M1 bars and validate OHLC (low <= open/close <= high), positive
   prices, strictly ascending, unique, minute-aligned.
3. Aggregate M1 -> M15/H4 with the SAME rules the backend uses. The bucket rule
   lives in ``bridge/mt5_python/mt5_csv_feed.py::CsvFeed._aggregate`` (the real
   engine code the packaged bridge serves), and the bar model is
   ``src/data/BarNormalizer.h``. We reuse that module directly (with
   ``FAKE_MT5_CSV_NO_SHIFT=1`` so the corpus is not time-shifted) and cross-check
   our own copy of the rule against it; disagreement is a failure.
4. Start a LOCAL ``astra_server`` (loopback only) with ``ASTRA_SENDER_TOKEN``,
   POST the real batches to ``/api/v1/candles/ingest``, replay them to prove
   idempotency, read them back via ``/api/v1/candles`` and compare field by
   field, then restart the server against the same data dir and read again to
   prove persistence.
5. Emit a provenance + result record (``--json-out``).

Timestamp convention
--------------------
The corpus timestamps carry no timezone label; they are broker server time as
written. They are parsed with ``timegm`` and treated as the epoch value sent to
the server (the same convention ``mt5_csv_feed`` uses). ``clock_offset_seconds``
is therefore 0: no offset was measured, so none is claimed. Every bar in the
corpus is historical relative to wall-clock, so the server's future-guard is not
exercised in a way that depends on the offset.

Usage
-----
    python3 tools/ayzr_agents/real_candle_replay.py                 # default window
    python3 tools/ayzr_agents/real_candle_replay.py --all           # full corpus
    python3 tools/ayzr_agents/real_candle_replay.py --server-url http://127.0.0.1:8790
"""

from __future__ import annotations

import argparse
import calendar
import csv
import hashlib
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from typing import Dict, List, Optional, Tuple

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_CSV = os.path.join(REPO_ROOT, "research", "data", "xauusd_m1",
                           "xauusd_m1_real.csv")
DEFAULT_CHECKSUMS = os.path.join(REPO_ROOT, "research", "data", "xauusd_m1",
                                 "checksums.sha256")
CSV_FEED_DIR = os.path.join(REPO_ROOT, "bridge", "mt5_python")

SYMBOL = "XAUUSD"
# MT5 timeframe -> seconds, mirroring bridge/mt5_python/mt5_csv_feed.py and
# src/mt5/Mt5BridgeContract.h (intervalMillis).
TF_SECONDS = {"M1": 60, "M15": 900, "H4": 14400}
BATCH_SIZE = 500
# The store's /api/v1/candles route caps `limit` at 1000.
API_LIMIT = 1000
BAR_KEYS = ("time", "open", "high", "low", "close", "tick_volume")


# --------------------------------------------------------------------------
# Corpus loading / validation
# --------------------------------------------------------------------------

def parse_epoch(value: str) -> int:
    """Parse 'YYYY-MM-DD HH:MM:SS' (or ISO 'T') as epoch seconds via timegm."""
    text = value.strip().replace("T", " ")
    if text.endswith("Z"):
        text = text[:-1]
    try:
        return int(calendar.timegm(time.strptime(text, "%Y-%m-%d %H:%M:%S")))
    except ValueError:
        return int(calendar.timegm(time.strptime(text, "%Y-%m-%d %H:%M")))


def sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_checksum(csv_path: str, checksums_path: str) -> Dict[str, object]:
    """Compare the corpus sha256 with the committed checksums file."""
    with open(checksums_path, "r", encoding="utf-8") as handle:
        entries: Dict[str, str] = {}
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(None, 1)
            if len(parts) != 2:
                continue
            digest, rel = parts
            entries[rel.lstrip("*").strip()] = digest
    rel_csv = os.path.relpath(csv_path, REPO_ROOT)
    expected = entries.get(rel_csv)
    actual = sha256_file(csv_path)
    return {
        "checksums_file": os.path.relpath(checksums_path, REPO_ROOT),
        "csv": rel_csv,
        "expected_sha256": expected,
        "actual_sha256": actual,
        "verified": expected is not None and expected == actual,
    }


def load_m1(csv_path: str) -> Tuple[List[dict], int]:
    rows: List[dict] = []
    unparseable = 0
    with open(csv_path, "r", encoding="utf-8", newline="") as handle:
        for raw in csv.DictReader(handle):
            try:
                rows.append({
                    "time": parse_epoch(raw["timestamp"]),
                    "open": float(raw["open"]),
                    "high": float(raw["high"]),
                    "low": float(raw["low"]),
                    "close": float(raw["close"]),
                    "tick_volume": int(float(raw.get("volume") or 0)),
                })
            except (KeyError, TypeError, ValueError):
                unparseable += 1
    rows.sort(key=lambda r: r["time"])
    return rows, unparseable


def validate_m1(rows: List[dict]) -> Dict[str, object]:
    """OHLC + sequence integrity. Reports defects, never repairs them."""
    violations: List[str] = []
    for row in rows:
        o, h, l, c = row["open"], row["high"], row["low"], row["close"]
        if not (o > 0 and h > 0 and l > 0 and c > 0):
            violations.append(f"non-positive price at {row['time']}")
        elif not (l <= o <= h and l <= c <= h):
            violations.append(f"OHLC ordering violated at {row['time']}")
        if row["time"] % 60 != 0:
            violations.append(f"open_time not minute-aligned at {row['time']}")
    for i in range(1, len(rows)):
        if rows[i]["time"] <= rows[i - 1]["time"]:
            violations.append(f"non-ascending at index {i}")
    return {
        "rows": len(rows),
        "ohlc_violations": len(violations),
        "first_open_epoch": rows[0]["time"] if rows else 0,
        "last_open_epoch": rows[-1]["time"] if rows else 0,
        "first_open_broker": time.strftime("%Y-%m-%d %H:%M:%S",
                                           time.gmtime(rows[0]["time"])) if rows else "",
        "last_open_broker": time.strftime("%Y-%m-%d %H:%M:%S",
                                          time.gmtime(rows[-1]["time"])) if rows else "",
        "sample_violations": violations[:10],
    }


# --------------------------------------------------------------------------
# Aggregation (identical rule to the backend's mt5_csv_feed.CsvFeed._aggregate)
# --------------------------------------------------------------------------

def aggregate(rows: List[dict], interval: int) -> List[dict]:
    """Timeframe-boundary bucketing: open=first, high=max, low=min, close=last,
    tick_volume=sum. The bucket is ``t - (t % interval)``."""
    buckets: Dict[int, dict] = {}
    for row in rows:
        bucket = row["time"] - (row["time"] % interval)
        bar = buckets.get(bucket)
        if bar is None:
            buckets[bucket] = {
                "time": bucket,
                "open": row["open"],
                "high": row["high"],
                "low": row["low"],
                "close": row["close"],
                "tick_volume": row["tick_volume"],
            }
        else:
            bar["high"] = max(bar["high"], row["high"])
            bar["low"] = min(bar["low"], row["low"])
            bar["close"] = row["close"]
            bar["tick_volume"] += row["tick_volume"]
    return [buckets[k] for k in sorted(buckets)]


def bucket_m1_counts(rows: List[dict], interval: int) -> Dict[int, int]:
    counts: Dict[int, int] = {}
    for row in rows:
        bucket = row["time"] - (row["time"] % interval)
        counts[bucket] = counts.get(bucket, 0) + 1
    return counts


def cross_check_backend_rules(csv_path: str, rows: List[dict]) -> Dict[str, object]:
    """Aggregate with the backend's own module and compare to our copy."""
    os.environ["FAKE_MT5_CSV_NO_SHIFT"] = "1"
    if CSV_FEED_DIR not in sys.path:
        sys.path.insert(0, CSV_FEED_DIR)
    try:
        import mt5_csv_feed  # type: ignore
    except Exception as exc:  # pragma: no cover - environment dependent
        return {"available": False, "reason": f"{type(exc).__name__}: {exc}"}
    feed = mt5_csv_feed.CsvFeed(csv_path, symbol=SYMBOL)
    out: Dict[str, object] = {"available": True, "shift_seconds": feed.shift_seconds,
                              "module_bars": feed.total_bars()}
    agree = True
    for tf, interval in (("M15", 900), ("H4", 14400)):
        theirs = [{k: b[k] for k in BAR_KEYS} for b in feed._aggregate(interval)]
        ours = aggregate(rows, interval)
        same = len(theirs) == len(ours) and all(
            all(a[k] == b[k] for k in BAR_KEYS) for a, b in zip(theirs, ours))
        out[f"{tf}_agrees"] = same
        out[f"{tf}_backend_buckets"] = len(theirs)
        agree = agree and same
    out["all_agree"] = agree
    return out


# --------------------------------------------------------------------------
# HTTP + local server lifecycle
# --------------------------------------------------------------------------

def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def find_server_binary(explicit: Optional[str]) -> Optional[str]:
    candidates = []
    if explicit:
        candidates.append(explicit)
    for rel in ("build-linux/astra_server", "build/astra_server"):
        candidates.append(os.path.join(REPO_ROOT, rel))
    for candidate in candidates:
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def http_json(method: str, base_url: str, path: str, token: Optional[str],
              payload: Optional[dict] = None, timeout: float = 60.0,
              retries: int = 4):
    """Request JSON. Retries only on 429/5xx with bounded backoff, per the
    sender contract; a 400 is terminal."""
    url = base_url.rstrip("/") + path
    data = json.dumps(payload).encode() if payload is not None else None
    attempt = 0
    while True:
        request = urllib.request.Request(url, data=data, method=method)
        if token:
            request.add_header("Authorization", f"Bearer {token}")
        if data is not None:
            request.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return response.status, json.loads(response.read().decode() or "{}")
        except urllib.error.HTTPError as exc:
            body = exc.read().decode() or "{}"
            if exc.code in (429, 500, 502, 503, 504) and attempt < retries:
                attempt += 1
                time.sleep(min(2.0 * attempt, 10.0))
                continue
            try:
                return exc.code, json.loads(body)
            except json.JSONDecodeError:
                return exc.code, {"raw": body}


class LocalServer:
    """Owns a loopback-only astra_server child process."""

    def __init__(self, binary: str, data_dir: str, token: str,
                 ingest_rate_per_min: int = 100000) -> None:
        self.binary = binary
        self.data_dir = data_dir
        self.token = token
        self.ingest_rate_per_min = ingest_rate_per_min
        self.port = free_port()
        self.proc: Optional[subprocess.Popen] = None

    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self.port}"

    def start(self, log_path: Optional[str] = None) -> bool:
        env = dict(os.environ)
        env["ASTRA_SENDER_TOKEN"] = self.token
        env["ASTRA_DATA_DIR"] = self.data_dir
        env["ASTRA_HOST"] = "127.0.0.1"
        env["ASTRA_LLM_PROVIDER"] = "mock"   # never spend live LLM quota
        env["PORT"] = str(self.port)
        # Raise the ingest rate ceiling so a full-corpus replay is not throttled
        # by the default 240/min; the tool still retries on 429 for any server
        # it did not start.
        env["ASTRA_INGEST_RATE_PER_MIN"] = str(self.ingest_rate_per_min)
        log = open(log_path, "ab") if log_path else subprocess.DEVNULL
        # Own session/process group: a signal directed at the parent's group
        # (job control, Ctrl-C, terminal teardown) must never reach a server the
        # workflow is still using, and stop() must target only this child.
        self.proc = subprocess.Popen(
            [self.binary, "--host", "127.0.0.1", "--port", str(self.port),
             "--data-dir", self.data_dir, "--sender-token", self.token],
            stdout=log, stderr=log, env=env, start_new_session=True)
        for _ in range(120):
            try:
                status, _ = http_json("GET", self.base_url, "/api/v1/health",
                                      None, timeout=1.0)
                if status == 200:
                    return True
            except Exception:
                pass
            if self.proc.poll() is not None:
                return False
            time.sleep(0.1)
        return False

    def stop(self) -> None:
        if self.proc is None:
            return
        self.proc.terminate()
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=5)
        self.proc = None


# --------------------------------------------------------------------------
# Comparison helpers
# --------------------------------------------------------------------------

def compare_bars(expected: List[dict], returned: List[dict]) -> Dict[str, object]:
    """Compare returned API bars to the bars we sent, field by field.

    Returns which contiguous slice matched ('oldest'/'newest') plus the max
    absolute price delta, so the caller never assumes a slice direction.
    """
    def slice_equal(a: List[dict], b: List[dict]) -> bool:
        if len(a) != len(b):
            return False
        return all(all(x[k] == y[k] for k in BAR_KEYS) for x, y in zip(a, b))

    result: Dict[str, object] = {"returned": len(returned)}
    if not returned:
        result["matched"] = None
        return result
    if len(returned) <= len(expected) and slice_equal(returned, expected[:len(returned)]):
        result["matched"] = "oldest"
    elif len(returned) <= len(expected) and slice_equal(returned, expected[-len(returned):]):
        result["matched"] = "newest"
    else:
        result["matched"] = None
    # Independent numeric closeness check (never used as the primary verdict).
    if result["matched"] and len(returned) <= len(expected):
        ref = expected[:len(returned)] if result["matched"] == "oldest" \
            else expected[-len(returned):]
        result["max_abs_price_delta"] = max(
            abs(r[k] - e[k]) for r, e in zip(returned, ref)
            for k in ("open", "high", "low", "close"))
    return result


def post_batches(base_url: str, token: str, source_id: str, timeframe: str,
                 bars: List[dict]) -> Dict[str, object]:
    accepted = duplicates = rejected = 0
    first_rejection = ""
    for start in range(0, len(bars), BATCH_SIZE):
        chunk = bars[start:start + BATCH_SIZE]
        payload = {
            "source_id": source_id,
            "symbol": SYMBOL,
            "timeframe": timeframe,
            "clock_offset_seconds": 0,
            "candles": chunk,
        }
        status, body = http_json("POST", base_url, "/api/v1/candles/ingest",
                                 token, payload)
        if status != 200:
            raise RuntimeError(f"ingest HTTP {status}: {json.dumps(body)[:300]}")
        data = body.get("data", {})
        accepted += int(data.get("ingested", 0))
        duplicates += len(data.get("duplicates", []))
        rej = data.get("rejected", [])
        rejected += len(rej)
        if rej and not first_rejection:
            first_rejection = json.dumps(rej[0])
    return {"sent": len(bars), "accepted": accepted, "duplicates": duplicates,
            "rejected": rejected, "first_rejection": first_rejection}


def sync_point(base_url: str, token: str, source_id: str,
               timeframe: str, timeout: float = 300.0) -> Dict[str, int]:
    start = time.time()
    status, body = http_json("GET", base_url,
                             f"/api/v1/sync/latest?source_id={source_id}",
                             token, timeout=timeout)
    elapsed = time.time() - start
    if status != 200:
        return {"sync_status": status, "sync_seconds": round(elapsed, 3)}
    for point in body.get("data", []):
        if point.get("timeframe") == timeframe and point.get("symbol") == SYMBOL:
            return {"last_open_time_utc": int(point.get("last_open_time_utc", 0)),
                    "count": int(point.get("count", 0)),
                    "sync_seconds": round(elapsed, 3)}
    return {"sync_seconds": round(elapsed, 3)}


def fetch_candles(base_url: str, timeframe: str, limit: int = API_LIMIT,
                  timeout: float = 300.0) -> Dict[str, object]:
    start = time.time()
    status, body = http_json("GET", base_url,
                             f"/api/v1/candles?tf={timeframe}&limit={limit}",
                             None, timeout=timeout)
    elapsed = time.time() - start
    if status != 200:
        return {"status": status, "bars": [], "candles_seconds": round(elapsed, 3)}
    data = body.get("data", {})
    bars = [{k: b[k] for k in BAR_KEYS} for b in data.get("bars", [])]
    return {"status": status, "bars": bars,
            "count": int(data.get("count", len(bars))),
            "newest_closed_time": int(data.get("newest_closed_time", 0)),
            "symbol": data.get("symbol", ""),
            "candles_seconds": round(elapsed, 3)}


# --------------------------------------------------------------------------
# Workflow
# --------------------------------------------------------------------------

def select_window(rows: List[dict], bars: int, use_all: bool) -> List[dict]:
    """Trailing window snapped down to an H4 boundary so every bucket except the
    corpus-truncated last one is complete."""
    if use_all or bars <= 0 or bars >= len(rows):
        return rows
    start = len(rows) - bars
    boundary = rows[start]["time"]
    boundary -= boundary % TF_SECONDS["H4"]
    index = 0
    for i, row in enumerate(rows):
        if row["time"] >= boundary:
            index = i
            break
    return rows[index:]


def scaling_probe(binary: str, token: str, rows: List[dict],
                  sizes: List[int]) -> Dict[str, object]:
    """Characterize the guarded read path (sync/candles) against series length.

    Each size gets a fresh server + store, then the real M1 rows are ingested and
    the read latency measured. The result is evidence about the read guard's
    cost, not a synthetic benchmark.
    """
    import shutil
    probe: Dict[str, object] = {}
    for size in sizes:
        window = select_window(rows, size, False)
        m1 = aggregate(window, TF_SECONDS["M1"])
        data_dir = tempfile.mkdtemp(prefix="astra-scaling-")
        server = LocalServer(binary, data_dir, token)
        if not server.start():
            probe[str(size)] = {"error": "server failed to start"}
            shutil.rmtree(data_dir, ignore_errors=True)
            continue
        try:
            ack = post_batches(server.base_url, token, "scaling-probe", "M1", m1)
            start = time.time()
            status, _ = http_json("GET", server.base_url,
                                  "/api/v1/sync/latest?source_id=scaling-probe",
                                  token, timeout=600)
            sync_seconds = time.time() - start
            fetch = fetch_candles(server.base_url, "M1")
            probe[str(len(m1))] = {
                "rows": len(m1),
                "ingested": ack["accepted"],
                "sync_status": status,
                "sync_seconds": round(sync_seconds, 2),
                "candles_seconds": fetch.get("candles_seconds"),
                "candles_returned": len(fetch.get("bars", [])),
            }
            print(f"  scaling: rows={len(m1)} sync={sync_seconds:.2f}s "
                  f"candles={fetch.get('candles_seconds')}s", flush=True)
        finally:
            server.stop()
            shutil.rmtree(data_dir, ignore_errors=True)
    return probe


def run_workflow(args) -> Dict[str, object]:
    result: Dict[str, object] = {"ok": False, "checks": {}, "errors": []}
    checks = result["checks"]

    checksum = verify_checksum(args.csv, args.checksums)
    checks["checksum"] = checksum
    if not checksum["verified"]:
        result["errors"].append(
            f"corpus checksum mismatch: expected {checksum['expected_sha256']} "
            f"got {checksum['actual_sha256']}")
        return result

    rows, unparseable = load_m1(args.csv)
    validation = validate_m1(rows)
    checks["validation"] = validation
    checks["unparseable_rows"] = unparseable
    if unparseable or validation["ohlc_violations"]:
        result["errors"].append(
            f"corpus validation failed: unparseable={unparseable} "
            f"violations={validation['ohlc_violations']}")
        return result

    checks["backend_aggregation_cross_check"] = cross_check_backend_rules(
        args.csv, rows)

    window = select_window(rows, args.bars, args.all_bars)
    timeframes = [tf.strip() for tf in args.timeframes.split(",") if tf.strip()]
    unknown = [tf for tf in timeframes if tf not in TF_SECONDS]
    if unknown:
        result["errors"].append(f"unsupported timeframe(s): {unknown}")
        return result
    aggregated = {tf: aggregate(window, TF_SECONDS[tf]) for tf in timeframes}
    counts = {tf: bucket_m1_counts(window, TF_SECONDS[tf]) for tf in timeframes}

    provenance = {
        "symbol": SYMBOL,
        "timeframes": list(timeframes),
        "corpus_rows": len(rows),
        "window_rows": len(window),
        "window_first_broker": time.strftime("%Y-%m-%d %H:%M:%S",
                                             time.gmtime(window[0]["time"])),
        "window_last_broker": time.strftime("%Y-%m-%d %H:%M:%S",
                                            time.gmtime(window[-1]["time"])),
        "timestamp_convention": "broker server time, parsed as epoch via timegm",
        "clock_offset_seconds": 0,
        "aggregation_rule": "bucket=t-(t%interval); open=first,high=max,low=min,"
                            "close=last,tick_volume=sum (mt5_csv_feed._aggregate)",
        "sha256": checksum["actual_sha256"],
        "source_id": args.source_id,
    }
    result["provenance"] = provenance

    for tf in timeframes:
        bars = aggregated[tf]
        last_bucket = bars[-1]["time"]
        provenance.setdefault("aggregates", {})[tf] = {
            "bars": len(bars),
            "first_open_epoch": bars[0]["time"],
            "last_open_epoch": last_bucket,
            "last_bucket_m1_count": counts[tf][last_bucket],
            "last_bucket_complete": counts[tf][last_bucket] == TF_SECONDS[tf] // 60,
        }

    if not args.server_url:
        binary = find_server_binary(args.server_bin)
        if binary is None:
            result["errors"].append("astra_server binary not found; build it first")
            return result
        data_dir = args.data_dir or tempfile.mkdtemp(prefix="astra-replay-")
        server = LocalServer(binary, data_dir, args.token)
        if not server.start(args.server_log):
            result["errors"].append("astra_server failed to start")
            return result
        result["data_dir"] = data_dir
        if args.scaling:
            print("=== guarded-read scaling probe ===", flush=True)
            checks["scaling"] = scaling_probe(binary, args.token, rows,
                                              [2000, 4000, 8000, 16000])
    else:
        server = None
        base_url = args.server_url

    try:
        base_url = server.base_url if server else args.server_url
        per_tf: Dict[str, object] = {}
        for tf in timeframes:
            bars = aggregated[tf]
            send = post_batches(base_url, args.token, args.source_id, tf, bars)
            replay = post_batches(base_url, args.token, args.source_id, tf, bars)
            fetched = fetch_candles(base_url, tf)
            comparison = compare_bars(bars, fetched["bars"])
            point = sync_point(base_url, args.token, args.source_id, tf)
            entry = {
                "bars_aggregated": len(bars),
                "send": send,
                "replay": replay,
                "api_status": fetched["status"],
                "api_count_field": fetched.get("count", 0),
                "api_returned": len(fetched["bars"]),
                "api_newest_closed_time": fetched.get("newest_closed_time", 0),
                "api_symbol": fetched.get("symbol", ""),
                "compare": comparison,
                "sync": point,
            }
            entry["idempotent"] = (replay["accepted"] == 0
                                   and replay["duplicates"] == len(bars)
                                   and replay["rejected"] == 0)
            entry["all_accepted"] = (send["accepted"] == len(bars)
                                     and send["rejected"] == 0)
            entry["api_matches_sent"] = comparison.get("matched") is not None
            entry["sync_count_matches"] = point.get("count", -1) == len(bars)
            entry["sync_watermark_matches"] = (
                point.get("last_open_time_utc", -1) == bars[-1]["time"])
            # Finding (not a failure): /api/v1/candles serves the OLDEST `limit`
            # rows, so for a series longer than the API cap `newest_closed_time`
            # is not the newest stored candle. Recorded so the claim is honest.
            entry["api_truncated"] = len(fetched["bars"]) < len(bars)
            entry["api_newest_closed_time_is_newest_stored"] = (
                fetched.get("newest_closed_time", 0) == bars[-1]["time"])
            per_tf[tf] = entry
        checks["replay"] = per_tf

        # Restart persistence: stop, start a fresh process over the same dir.
        if server is not None:
            server.stop()
            restarted = LocalServer(binary, data_dir, args.token)
            if not restarted.start(args.server_log):
                result["errors"].append("astra_server failed to restart")
                return result
            server = restarted
            persistence: Dict[str, object] = {}
            for tf in timeframes:
                fetched = fetch_candles(base_url=server.base_url, timeframe=tf)
                comparison = compare_bars(aggregated[tf], fetched["bars"])
                point = sync_point(server.base_url, args.token, args.source_id, tf)
                persistence[tf] = {
                    "api_returned": len(fetched["bars"]),
                    "compare": comparison,
                    "sync": point,
                    "survived": (comparison.get("matched") is not None
                                 and point.get("count", -1) == len(aggregated[tf])),
                }
            checks["restart_persistence"] = persistence
    finally:
        if server is not None:
            server.stop()
            if not args.keep_data and not args.data_dir:
                import shutil
                shutil.rmtree(server.data_dir, ignore_errors=True)

    # Verdict: every check that must hold, holds.
    failures = []
    for tf, entry in checks.get("replay", {}).items():
        if not entry["all_accepted"]:
            failures.append(f"{tf}: batch not fully accepted")
        if not entry["idempotent"]:
            failures.append(f"{tf}: replay not idempotent")
        if not entry["api_matches_sent"]:
            failures.append(f"{tf}: API candles do not match sent bars")
        if not entry["sync_count_matches"]:
            failures.append(f"{tf}: sync count mismatch")
        if not entry["sync_watermark_matches"]:
            failures.append(f"{tf}: sync watermark mismatch")
    for tf, entry in checks.get("restart_persistence", {}).items():
        if not entry["survived"]:
            failures.append(f"{tf}: data did not survive restart")
    xcheck = checks.get("backend_aggregation_cross_check", {})
    if xcheck.get("available") and not xcheck.get("all_agree", False):
        failures.append("aggregation disagrees with the backend module")

    result["failures"] = failures
    result["ok"] = not failures and not result["errors"]
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--csv", default=DEFAULT_CSV)
    parser.add_argument("--checksums", default=DEFAULT_CHECKSUMS)
    parser.add_argument("--server-bin", default=None)
    parser.add_argument("--server-url", default=None,
                        help="use an already-running server instead of starting one")
    parser.add_argument("--data-dir", default=None)
    parser.add_argument("--token", default="real-replay-token")
    parser.add_argument("--source-id", default="agent-c-real-replay")
    parser.add_argument("--bars", type=int, default=4000,
                        help="trailing M1 bars to replay (snapped to an H4 boundary)")
    parser.add_argument("--all", dest="all_bars", action="store_true",
                        help="replay the entire corpus")
    parser.add_argument("--timeframes", default="M15,H4",
                        help="comma-separated timeframes to replay (default M15,H4). "
                             "M1 on the full corpus is intentionally opt-in: the "
                             "guarded read path is quadratic in series length, so a "
                             "100k-row M1 read is minutes, not seconds (see --scaling).")
    parser.add_argument("--server-log", default=None)
    parser.add_argument("--scaling", action="store_true",
                        help="also probe guarded-read latency vs series length")
    parser.add_argument("--json-out", default=None)
    parser.add_argument("--keep-data", action="store_true")
    args = parser.parse_args()

    if args.server_url and "127.0.0.1" not in args.server_url \
            and "localhost" not in args.server_url:
        raise SystemExit("refusing to run against a non-loopback server")

    result = run_workflow(args)

    print("=== provenance ===")
    print(json.dumps(result.get("provenance", {}), indent=2))
    print("=== checks ===")
    print(json.dumps(result.get("checks", {}), indent=2))
    for error in result.get("errors", []):
        print(f"ERROR: {error}")
    for failure in result.get("failures", []):
        print(f"FAILURE: {failure}")
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as handle:
            json.dump(result, handle, indent=2, sort_keys=True)
        print(f"wrote {args.json_out}")
    print("RESULT:", "PASS" if result["ok"] else "FAIL")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
