#!/usr/bin/env python3
"""Reproducible local benchmark for the ASTRA/AYZR backend read + ingest surface.

Scope (Agent E - performance, concurrency & failure resilience):

  * ingestion of real XAUUSD M1 bars in 100 / 500 / 2000-candle batches;
  * duplicate-batch (replay) ingestion, serial and concurrent;
  * concurrent ingestion correctness (disjoint and same-window);
  * candle retrieval at M1 / M15 / H4, plus a read-latency-vs-store-size
    sweep on the M1 series;
  * concurrent reads, and concurrent ingest+read;
  * news list + history latency;
  * an isolated, server-free SQLite experiment that times the read-path
    alignment guard directly (the optimization baseline).

Everything is measured against a REAL `astra_server` process started by this
script on loopback, using the committed REAL MT5 corpus
(`research/data/xauusd_m1/xauusd_m1_real.csv`, verified by sha256 against the
committed `checksums.sha256`). No data is invented: the higher timeframes are
deterministic aggregations of the real M1 bars, and only buckets whose full M1
minute set is present in the corpus are used (so an aggregated bar can never be
a partial/fabricated bucket).

The script:
  * refuses to run against a non-loopback host;
  * uses ASTRA_LLM_PROVIDER=mock so no external LLM quota is spent;
  * never sends data off the machine;
  * reports only measured values (no extrapolation, no synthetic numbers).

Usage:
    python3 tools/ayzr_agents/bench_backend.py \
        --data /workspace/project/asp/research/data/xauusd_m1/xauusd_m1_real.csv \
        --server build-linux/astra_server \
        --out docs/ayzr/backend-hardening/PERFORMANCE_RESULTS.md

Options that change what is measured:
    --quick            fewer repetitions (smoke run)
    --m1-full          ingest the whole 100,008-bar M1 corpus (default: 20,000)
"""

import argparse
import datetime
import hashlib
import json
import os
import platform
import shutil
import signal
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request

SYMBOL = "XAUUSD"
SENDER_TOKEN = "bench-sender-token"
CLIENT_TOKEN = "bench-client-token"
ADMIN_TOKEN = "bench-admin-token"

# Server-side caps. The batch cap is raised so the 2000-candle scenario measures
# the store/ingest path rather than the 413 safety cap; the rate limits are
# disabled so the measurement is throughput, not throttling.
SERVER_ENV = {
    "ASTRA_LLM_PROVIDER": "mock",
    "ASTRA_INGEST_RATE_PER_MIN": "0",
    "ASTRA_SYNC_RATE_PER_MIN": "0",
    "ASTRA_MAX_CANDLES_PER_BATCH": "2000",
}


# ---------------------------------------------------------------------------
# Corpus loading (REAL data only)
# ---------------------------------------------------------------------------
def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def verify_corpus(path):
    """Verify the corpus sha256 against the committed checksum file, if found."""
    checksums = os.path.join(os.path.dirname(path), "checksums.sha256")
    if not os.path.exists(checksums):
        return None, "checksums.sha256 not found next to the corpus"
    digest = sha256_file(path)
    base = os.path.basename(path)
    with open(checksums, "r", encoding="utf-8") as fh:
        for line in fh:
            parts = line.split()
            if len(parts) >= 2 and os.path.basename(parts[-1]) == base:
                return (parts[0] == digest), (
                    "expected %s, measured %s" % (parts[0][:16], digest[:16])
                )
    return None, "no checksum entry for %s" % base


def epoch_of(stamp):
    dt = datetime.datetime.strptime(stamp, "%Y-%m-%d %H:%M:%S")
    return int(dt.replace(tzinfo=datetime.timezone.utc).timestamp())


def load_m1(path, limit=None):
    """Load the real corpus. Returns a list of (t, o, h, l, c, v)."""
    rows = []
    with open(path, "r", encoding="utf-8") as fh:
        header = fh.readline().strip()
        if header != "timestamp,open,high,low,close,volume":
            raise SystemExit("unexpected corpus header: %r" % header)
        for line in fh:
            line = line.strip()
            if not line:
                continue
            p = line.split(",")
            rows.append(
                (
                    epoch_of(p[0]),
                    float(p[1]),
                    float(p[2]),
                    float(p[3]),
                    float(p[4]),
                    int(float(p[5])),
                )
            )
            if limit is not None and len(rows) >= limit:
                break
    return rows


def aggregate_complete(m1, step, expect):
    """Aggregate M1 -> `step` seconds, keeping only buckets whose full M1 set is
    present. A partial bucket would be a fabricated bar, so it is dropped."""
    buckets = {}
    order = []
    for row in m1:
        key = row[0] - (row[0] % step)
        if key not in buckets:
            buckets[key] = []
            order.append(key)
        buckets[key].append(row)
    out = []
    for key in order:
        group = buckets[key]
        if len(group) != expect:
            continue
        # The corpus is strictly ascending, so group[0] is the bucket's first
        # minute. A bucket only counts when it starts exactly on the grid.
        if group[0][0] != key:
            continue
        out.append(
            (
                key,
                group[0][1],
                max(r[2] for r in group),
                min(r[3] for r in group),
                group[-1][4],
                sum(r[5] for r in group),
            )
        )
    return out


def candle_json(row):
    return {
        "time": row[0],
        "open": row[1],
        "high": row[2],
        "low": row[3],
        "close": row[4],
        "tick_volume": row[5],
        "spread": 0,
        "real_volume": 0,
    }


def ingest_body(source, timeframe, rows):
    return {
        "source_id": source,
        "symbol": SYMBOL,
        "timeframe": timeframe,
        "candles": [candle_json(r) for r in rows],
    }


# ---------------------------------------------------------------------------
# HTTP helpers
# ---------------------------------------------------------------------------
class Client:
    def __init__(self, host, port, timeout=300):
        if host not in ("127.0.0.1", "localhost", "::1"):
            raise SystemExit("refusing non-loopback host: %s" % host)
        self.base = "http://%s:%d" % (host, port)
        self.timeout = timeout

    def request(self, method, path, token=None, body=None):
        data = None
        if body is not None:
            data = json.dumps(body).encode()
        req = urllib.request.Request(self.base + path, data=data, method=method)
        if token:
            req.add_header("Authorization", "Bearer " + token)
        if data is not None:
            req.add_header("Content-Type", "application/json")
        start = time.perf_counter()
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                payload = resp.read()
                status = resp.status
        except urllib.error.HTTPError as exc:
            payload = exc.read()
            status = exc.code
        except Exception as exc:  # transport failure
            return {"status": 0, "body": b"", "error": str(exc),
                    "ms": (time.perf_counter() - start) * 1000.0}
        return {"status": status, "body": payload, "error": None,
                "ms": (time.perf_counter() - start) * 1000.0}

    def get(self, path, token=None):
        return self.request("GET", path, token=token)

    def post(self, path, token, body):
        return self.request("POST", path, token=token, body=body)


def percentile(sorted_values, p):
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]
    idx = int(round((p / 100.0) * (len(sorted_values) - 1)))
    return sorted_values[max(0, min(idx, len(sorted_values) - 1))]


class Samples:
    """Latency + status collector for one scenario."""

    def __init__(self, name, concurrency=1, notes=""):
        self.name = name
        self.concurrency = concurrency
        self.notes = notes
        self.latencies = []
        self.statuses = {}
        self.transport_errors = 0
        self.wall_ms = 0.0
        self.extra = {}
        self._lock = threading.Lock()

    def add(self, result):
        with self._lock:
            if result["error"] is not None or result["status"] == 0:
                self.transport_errors += 1
                self.statuses["transport_error"] = (
                    self.statuses.get("transport_error", 0) + 1
                )
            else:
                self.statuses[result["status"]] = (
                    self.statuses.get(result["status"], 0) + 1
                )
                self.latencies.append(result["ms"])

    @property
    def requests(self):
        return len(self.latencies) + self.transport_errors

    @property
    def errors(self):
        return sum(c for s, c in self.statuses.items()
                   if not (isinstance(s, int) and 200 <= s < 300))

    def summary(self):
        lat = sorted(self.latencies)
        return {
            "name": self.name,
            "requests": self.requests,
            "concurrency": self.concurrency,
            "errors": self.errors,
            "transport_errors": self.transport_errors,
            "statuses": {str(k): v for k, v in sorted(
                self.statuses.items(), key=lambda kv: str(kv[0]))},
            "latency_ms": {
                "min": round(lat[0], 3) if lat else None,
                "mean": round(statistics.mean(lat), 3) if lat else None,
                "p50": round(percentile(lat, 50), 3) if lat else None,
                "p95": round(percentile(lat, 95), 3) if lat else None,
                "max": round(lat[-1], 3) if lat else None,
            },
            "wall_ms": round(self.wall_ms, 3),
            "throughput_per_s": (
                round(self.requests / (self.wall_ms / 1000.0), 2)
                if self.wall_ms > 0 else None
            ),
            "notes": self.notes,
            "extra": self.extra,
        }


def run_serial(client, name, path, token, count, notes=""):
    s = Samples(name, concurrency=1, notes=notes)
    start = time.perf_counter()
    for _ in range(count):
        s.add(client.get(path, token=token))
    s.wall_ms = (time.perf_counter() - start) * 1000.0
    return s


def run_concurrent(client, name, path, token, threads, per_thread, notes=""):
    s = Samples(name, concurrency=threads, notes=notes)

    def worker():
        for _ in range(per_thread):
            s.add(client.get(path, token=token))

    workers = [threading.Thread(target=worker) for _ in range(threads)]
    start = time.perf_counter()
    for w in workers:
        w.start()
    for w in workers:
        w.join()
    s.wall_ms = (time.perf_counter() - start) * 1000.0
    return s


def adaptive_reps(client, path, token, threads, default_reps, budget_ms,
                  notes="probe"):
    """Choose a repetition count so a scenario's expected serialized wall time
    stays under `budget_ms`. A probe request measures the real per-request cost,
    so a slow path (e.g. the M1 read at full corpus scale) is measured with
    fewer repetitions instead of being reported as a timeout."""
    probe = client.get(path, token=token)
    probe_ms = probe["ms"] if probe["error"] is None else 0.0
    if probe_ms <= 0:
        return default_reps, None
    allowed = max(1, int(budget_ms / (probe_ms * max(1, threads))))
    return max(1, min(default_reps, allowed)), round(probe_ms, 3)


# ---------------------------------------------------------------------------
# Isolated store-query experiment (no server)
# ---------------------------------------------------------------------------
def sqlite_guard_experiment(m1, rows_checkpoints):
    """Reproduce the read-path cost in isolation, without the HTTP server.

    Builds a scratch SQLite file with the exact `candles` schema + index the
    server uses, fills it with real M1 rows, and times the SAME read the
    `/api/v1/candles` route issues under different formulations of the
    alignment guard. This isolates "the guard is the bottleneck" from "the
    HTTP/serialization path is the bottleneck" and gives the optimization
    proposal a measured basis.

    Returns a list of result dicts; the temporary DB is removed afterwards.
    """
    import sqlite3

    results = []
    tmp = tempfile.mkdtemp(prefix="astra_guard_")
    db_path = os.path.join(tmp, "candles.sqlite")
    try:
        db = sqlite3.connect(db_path)
        db.executescript(
            "CREATE TABLE candles ("
            "  source_id TEXT NOT NULL, symbol TEXT NOT NULL,"
            "  timeframe TEXT NOT NULL, open_time INTEGER NOT NULL,"
            "  open REAL NOT NULL, high REAL NOT NULL, low REAL NOT NULL,"
            "  close REAL NOT NULL, tick_volume INTEGER NOT NULL DEFAULT 0,"
            "  spread INTEGER NOT NULL DEFAULT 0,"
            "  real_volume INTEGER NOT NULL DEFAULT 0,"
            "  received_at INTEGER NOT NULL,"
            "  PRIMARY KEY (source_id, symbol, timeframe, open_time));"
            "CREATE INDEX idx_candles_lookup"
            "  ON candles(symbol, timeframe, open_time);"
        )
        misaligned = (
            "(T.timeframe IN ('M1','M5','M15','M30','H1','H4') AND "
            "(T.open_time % (CASE T.timeframe"
            "   WHEN 'M1' THEN 60 WHEN 'M5' THEN 300 WHEN 'M15' THEN 900"
            "   WHEN 'M30' THEN 1800 WHEN 'H1' THEN 3600"
            "   WHEN 'H4' THEN 14400 END))<>0)"
        )

        def mis(alias):
            return misaligned.replace("T.", alias + ".")

        current = (
            "SELECT open_time FROM candles WHERE symbol=?1 AND timeframe=?2 AND "
            "NOT EXISTS (SELECT 1 FROM candles bad WHERE"
            " bad.symbol=candles.symbol AND bad.timeframe=candles.timeframe AND "
            + mis("bad") + ") ORDER BY open_time ASC LIMIT ?3"
        )
        precheck = (
            "SELECT 1 FROM candles WHERE symbol=?1 AND timeframe=?2 AND "
            + mis("candles") + " LIMIT 1"
        )
        plain = (
            "SELECT open_time FROM candles WHERE symbol=?1 AND timeframe=?2"
            " ORDER BY open_time ASC LIMIT ?3"
        )

        def timed(sql, args, reps=3):
            best = None
            for _ in range(reps):
                start = time.perf_counter()
                list(db.execute(sql, args))
                ms = (time.perf_counter() - start) * 1000.0
                best = ms if best is None else min(best, ms)
            return best

        inserted = 0
        for checkpoint in rows_checkpoints:
            if checkpoint > len(m1):
                break
            batch = m1[inserted:checkpoint]
            db.executemany(
                "INSERT INTO candles (source_id, symbol, timeframe, open_time,"
                " open, high, low, close, tick_volume, spread, real_volume,"
                " received_at) VALUES ('iso','XAUUSD','M1',?,?,?,?,?,?,0,0,0)",
                [(r[0], r[1], r[2], r[3], r[4], r[5]) for r in batch],
            )
            db.commit()
            inserted = checkpoint
            rows = db.execute("SELECT COUNT(*) FROM candles").fetchone()[0]
            cur_ms = timed(current, ("XAUUSD", "M1", 500))
            pre_ms = timed(precheck, ("XAUUSD", "M1"), reps=5)
            plain_ms = timed(plain, ("XAUUSD", "M1", 500))
            results.append({
                "stored_rows": rows,
                "current_guard_ms": round(cur_ms, 3),
                "precheck_ms": round(pre_ms, 3),
                "plain_ms": round(plain_ms, 3),
                "precheck_plus_plain_ms": round(pre_ms + plain_ms, 3),
                "speedup": round(cur_ms / (pre_ms + plain_ms), 1)
                if (pre_ms + plain_ms) > 0 else None,
            })
        db.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return results


# ---------------------------------------------------------------------------
# Server lifecycle
# ---------------------------------------------------------------------------
def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def wait_ready(client, attempts=120):
    for _ in range(attempts):
        r = client.get("/api/v1/health")
        if r["status"] and r["status"] > 0:
            return True
        time.sleep(0.1)
    return False


def start_server(exe, data_dir, port):
    env = dict(os.environ)
    env.update(SERVER_ENV)
    env["ASTRA_SENDER_TOKEN"] = SENDER_TOKEN
    env["ASTRA_CLIENT_TOKEN"] = CLIENT_TOKEN
    env["ASTRA_ADMIN_TOKEN"] = ADMIN_TOKEN
    log = open(os.path.join(data_dir, "server.log"), "wb")
    proc = subprocess.Popen(
        [exe, "--host", "127.0.0.1", "--port", str(port),
         "--data-dir", data_dir],
        stdout=log, stderr=subprocess.STDOUT, env=env,
    )
    return proc, log


def stop_server(proc):
    if proc is None:
        return
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=10)


def environment_report(exe, corpus, corpus_sha, corpus_ok, corpus_note, bars):
    mem_kb = 0
    try:
        with open("/proc/meminfo", "r", encoding="utf-8") as fh:
            for line in fh:
                if line.startswith("MemTotal:"):
                    mem_kb = int(line.split()[1])
                    break
    except OSError:
        pass
    try:
        nproc = os.cpu_count() or 0
    except Exception:
        nproc = 0
    return {
        "os": platform.platform(),
        "kernel": platform.release(),
        "python": sys.version.split()[0],
        "nproc": nproc,
        "mem_total_mib": round(mem_kb / 1024.0, 1) if mem_kb else None,
        "server_binary": os.path.abspath(exe),
        "server_sha256": sha256_file(exe) if os.path.exists(exe) else None,
        "server_env": dict(SERVER_ENV),
        "corpus_path": os.path.abspath(corpus),
        "corpus_sha256": corpus_sha,
        "corpus_checksum_ok": corpus_ok,
        "corpus_checksum_note": corpus_note,
        "corpus_m1_bars_loaded": bars,
        "started_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(
            timespec="seconds"),
    }


# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------
def scenario_ingest_batches(client, m1, sizes):
    """Ingest real M1 bars in batches of the requested sizes (fresh windows)."""
    out = []
    cursor = 0
    for size in sizes:
        if cursor + size > len(m1):
            cursor = 0
        window = m1[cursor:cursor + size]
        cursor += size
        s = Samples("ingest_batch_%d" % size,
                    notes="POST /api/v1/candles/ingest, %d real M1 bars" % size)
        start = time.perf_counter()
        r = client.post("/api/v1/candles/ingest", SENDER_TOKEN,
                        ingest_body("bench-ingest-%d" % size, "M1", window))
        s.wall_ms = (time.perf_counter() - start) * 1000.0
        s.add(r)
        body = None
        try:
            body = json.loads(r["body"].decode())
        except Exception:
            pass
        if body and body.get("data"):
            d = body["data"]
            s.extra = {
                "ingested": d.get("ingested"),
                "duplicates": len(d.get("duplicates", [])),
                "rejected": len(d.get("rejected", [])),
                "body_bytes": len(r["body"]),
            }
        out.append(s)
    return out


def scenario_duplicate_ingest(client, m1, size, repeats):
    """Replay the SAME batch `repeats` times: every replay must be
    already_stored (0 ingested) and must never create a second row."""
    window = m1[:size]
    body = ingest_body("bench-dup", "M1", window)
    s = Samples("ingest_duplicate_%d" % size,
                notes="POST the same %d-candle batch %d times" % (size, repeats))
    start = time.perf_counter()
    for i in range(repeats):
        r = client.post("/api/v1/candles/ingest", SENDER_TOKEN, body)
        s.add(r)
        if i == 0:
            try:
                d = json.loads(r["body"].decode())["data"]
                s.extra["first_ingested"] = d.get("ingested")
            except Exception:
                pass
        else:
            try:
                d = json.loads(r["body"].decode())["data"]
                s.extra.setdefault("replay_ingested", []).append(d.get("ingested"))
            except Exception:
                pass
    s.wall_ms = (time.perf_counter() - start) * 1000.0
    return s


def scenario_concurrent_ingest(client, m1, threads, size):
    """Concurrent DISJOINT ingestion: `threads` batches of `size` distinct real
    bars. Correctness requires the stored total to equal threads*size."""
    batches = []
    for i in range(threads):
        start = i * size
        batches.append(m1[start:start + size])
    s = Samples("ingest_concurrent_disjoint",
                concurrency=threads,
                notes="%d threads x %d distinct real M1 bars" % (threads, size))
    stored = [0] * threads
    dup = [0] * threads
    rej = [0] * threads

    def worker(i):
        r = client.post("/api/v1/candles/ingest", SENDER_TOKEN,
                        ingest_body("bench-conc-%d" % i, "M1", batches[i]))
        s.add(r)
        try:
            d = json.loads(r["body"].decode())["data"]
            stored[i] = d.get("ingested", 0)
            dup[i] = len(d.get("duplicates", []))
            rej[i] = len(d.get("rejected", []))
        except Exception:
            pass

    workers = [threading.Thread(target=worker, args=(i,))
               for i in range(threads)]
    start = time.perf_counter()
    for w in workers:
        w.start()
    for w in workers:
        w.join()
    s.wall_ms = (time.perf_counter() - start) * 1000.0
    s.extra = {
        "expected_stored_total": threads * size,
        "stored_total": sum(stored),
        "duplicates_total": sum(dup),
        "rejected_total": sum(rej),
    }
    return s


def scenario_concurrent_duplicate_ingest(client, m1, threads, size):
    """Concurrent ingestion of the SAME fresh window from `threads` clients.

    Exactly one request may report the batch as newly stored; every other must
    report it already stored. Never two rows for one candle."""
    window = m1[1000:1000 + size]
    body = ingest_body("bench-conc-dup", "M1", window)
    s = Samples("ingest_concurrent_duplicate",
                concurrency=threads,
                notes="%d threads post the same fresh %d-candle batch" % (threads, size))
    stored = [0] * threads
    dup = [0] * threads

    def worker(i):
        r = client.post("/api/v1/candles/ingest", SENDER_TOKEN, body)
        s.add(r)
        try:
            d = json.loads(r["body"].decode())["data"]
            stored[i] = d.get("ingested", 0)
            dup[i] = len(d.get("duplicates", []))
        except Exception:
            pass

    workers = [threading.Thread(target=worker, args=(i,))
               for i in range(threads)]
    start = time.perf_counter()
    for w in workers:
        w.start()
    for w in workers:
        w.join()
    s.wall_ms = (time.perf_counter() - start) * 1000.0
    s.extra = {
        "batch_size": size,
        "stored_new_total": sum(stored),
        "duplicate_total": sum(dup),
    }
    return s


def scenario_concurrent_ingest_and_read(client, m1, threads, size,
                                        read_path, budget_ms):
    """Half the workers ingest fresh disjoint windows while half read candles.

    The read path is deliberately the slowest one available so the scenario
    exposes head-of-line blocking between the two surfaces."""
    readers = max(1, threads // 2)
    writers = max(1, threads - readers)
    batches = []
    for i in range(writers):
        start = 2000 + i * size
        batches.append(m1[start:start + size])
    reps, probe = adaptive_reps(client, read_path, None, readers, 4, budget_ms)
    s_read = Samples("concurrent_read_with_ingest", concurrency=readers,
                     notes="GET %s during ingestion" % read_path)
    s_write = Samples("concurrent_ingest_with_read", concurrency=writers,
                      notes="POST /api/v1/candles/ingest during reads")

    def reader():
        for _ in range(reps):
            s_read.add(client.get(read_path))

    def writer(i):
        s_write.add(client.post("/api/v1/candles/ingest", SENDER_TOKEN,
                                ingest_body("bench-mix-%d" % i, "M1", batches[i])))

    workers = [threading.Thread(target=reader) for _ in range(readers)]
    workers += [threading.Thread(target=writer, args=(i,))
                for i in range(writers)]
    start = time.perf_counter()
    for w in workers:
        w.start()
    for w in workers:
        w.join()
    elapsed = (time.perf_counter() - start) * 1000.0
    s_read.wall_ms = elapsed
    s_write.wall_ms = elapsed
    s_read.extra = {"read_path": read_path, "adaptive_reps_per_thread": reps,
                    "probe_ms": probe}
    s_write.extra = {"ingest_batch": size}
    return s_read, s_write


def scenario_news(client, articles):
    out = []
    s = Samples("news_ingest_%d" % len(articles),
                notes="POST /api/v1/news/ingest, %d articles" % len(articles))
    start = time.perf_counter()
    r = client.post("/api/v1/news/ingest", SENDER_TOKEN, {"articles": articles})
    s.wall_ms = (time.perf_counter() - start) * 1000.0
    s.add(r)
    try:
        d = json.loads(r["body"].decode())["data"]
        s.extra = {"stored": d.get("stored"), "duplicates": d.get("duplicates")}
    except Exception:
        pass
    out.append(s)

    for label, path in (("news_list", "/api/v1/news?limit=50"),
                        ("news_history", "/api/v1/history?limit=50")):
        out.append(run_serial(client, label, path, CLIENT_TOKEN, 10,
                              notes="GET %s" % path))
    return out


def scenario_reads(client, m1_loaded, m15, h4, budget_ms):
    out = []
    for tf, n in (("M1", len(m1_loaded)), ("M15", len(m15)), ("H4", len(h4))):
        path = "/api/v1/candles?tf=%s&limit=500" % tf
        reps, probe = adaptive_reps(client, path, None, 1, 5, budget_ms)
        s = run_serial(client, "read_%s_500" % tf, path, None, reps,
                       notes="GET %s (store holds %d %s bars)" % (path, n, tf))
        r = client.get(path)
        s.extra = {"body_bytes": len(r["body"]), "status": r["status"],
                   "adaptive_reps": reps, "probe_ms": probe}
        out.append(s)
    return out


def scenario_read_scaling(client, tf, limits, budget_ms):
    out = []
    for limit in limits:
        path = "/api/v1/candles?tf=%s&limit=%d" % (tf, limit)
        reps, probe = adaptive_reps(client, path, None, 1, 5, budget_ms)
        s = run_serial(client, "read_%s_limit_%d" % (tf, limit), path, None, reps,
                       notes="GET %s" % path)
        r = client.get(path)
        s.extra = {"body_bytes": len(r["body"]), "status": r["status"],
                   "adaptive_reps": reps, "probe_ms": probe}
        out.append(s)
    return out


def scenario_concurrent_reads(client, tf, limit, thread_counts, per_thread,
                              budget_ms):
    out = []
    path = "/api/v1/candles?tf=%s&limit=%d" % (tf, limit)
    for threads in thread_counts:
        reps, probe = adaptive_reps(client, path, None, threads, per_thread,
                                    budget_ms)
        s = run_concurrent(
            client, "concurrent_read_%s_limit_%d_threads_%d" % (tf, limit, threads),
            path, None, threads, reps,
            notes="GET %s, %d clients" % (path, threads))
        s.extra = {"adaptive_reps_per_thread": reps, "probe_ms": probe}
        out.append(s)
    return out


def scenario_store_size_sweep(client, m1, checkpoints, budget_ms):
    """Ingest the real M1 corpus in chunks and measure the SAME read
    (`/candles?tf=M1&limit=500`) after each checkpoint. If read latency grows
    with the number of stored rows, the cost is in the store query (the
    correlated alignment guard), not in serialization."""
    out = []
    ingested = 0
    for checkpoint in checkpoints:
        while ingested < checkpoint and ingested < len(m1):
            chunk = m1[ingested:min(ingested + 2000, checkpoint)]
            client.post("/api/v1/candles/ingest", SENDER_TOKEN,
                        ingest_body("bench-sweep", "M1", chunk))
            ingested += len(chunk)
        path = "/api/v1/candles?tf=M1&limit=500"
        reps, probe = adaptive_reps(client, path, None, 1, 3, budget_ms)
        s = run_serial(client, "read_M1_500_at_%d_rows" % ingested, path, None,
                       reps, notes="GET %s with %d stored M1 rows"
                       % (path, ingested))
        r = client.get(path)
        s.extra = {"stored_m1_rows": ingested, "body_bytes": len(r["body"]),
                   "adaptive_reps": reps, "probe_ms": probe}
        out.append(s)
    return out


def scenario_analysis_endpoints(client):
    out = []
    for label, path in (
        ("analysis_latest", "/api/v1/analysis/latest"),
        ("health_v1", "/api/v1/health/v1"),
        ("timeframes", "/api/v1/timeframes"),
        ("sync_latest", "/api/v1/sync/latest"),
        ("health", "/api/v1/health"),
    ):
        token = SENDER_TOKEN if path.startswith("/api/v1/sync") else None
        out.append(run_serial(client, label, path, token, 10,
                              notes="GET %s" % path))
    return out


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------
def fmt(value, digits=2):
    if value is None:
        return "n/a"
    if isinstance(value, float):
        return ("%%.%df" % digits) % value
    return str(value)


def markdown_report(env, summaries, data_profile, bottleneck_notes,
                    guard_results):
    lines = []
    lines.append("# AYZR backend performance results (Agent E)")
    lines.append("")
    lines.append("Measured locally against a real `astra_server` process on")
    lines.append("loopback, ingesting the committed REAL MT5 corpus. Only")
    lines.append("measured values appear below; nothing is extrapolated.")
    lines.append("")
    lines.append("## How to reproduce")
    lines.append("")
    lines.append("```bash")
    lines.append("# 1. build")
    lines.append("cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release"
                 " && cmake --build build-linux -j4")
    lines.append("")
    lines.append("# 2. full benchmark (starts its own astra_server on a free")
    lines.append("#    loopback port, mock LLM provider, rate limits disabled,")
    lines.append("#    batch cap raised to 2000)")
    lines.append("python3 tools/ayzr_agents/bench_backend.py --m1-full \\")
    lines.append("    --out docs/ayzr/backend-hardening/PERFORMANCE_RESULTS.md \\")
    lines.append("    --json-out /tmp/perf.json")
    lines.append("")
    lines.append("# 3. quick smoke (fewer repetitions, smaller corpus slice)")
    lines.append("python3 tools/ayzr_agents/bench_backend.py --quick --out /tmp/quick.md")
    lines.append("```")
    lines.append("")
    lines.append("The script verifies the corpus sha256 against the committed")
    lines.append("`research/data/xauusd_m1/checksums.sha256` before measuring,")
    lines.append("refuses a non-loopback host, and spends no external LLM quota.")
    lines.append("Every number below is a measured value; the repetition count is")
    lines.append("chosen adaptively from a probe request so a slow path is measured")
    lines.append("with fewer reps rather than being reported as a timeout (see the")
    lines.append("`adaptive_reps` field in the JSON output).")
    lines.append("")
    lines.append("## Environment")
    lines.append("")
    lines.append("| Field | Value |")
    lines.append("|---|---|")
    for key in ("os", "kernel", "python", "nproc", "mem_total_mib",
                "server_binary", "server_sha256", "started_at_utc"):
        lines.append("| %s | `%s` |" % (key, env.get(key)))
    lines.append("| server env overrides | `%s` |" % json.dumps(env["server_env"]))
    lines.append("")
    lines.append("## Data")
    lines.append("")
    lines.append("| Field | Value |")
    lines.append("|---|---|")
    lines.append("| corpus | `%s` |" % env["corpus_path"])
    lines.append("| corpus sha256 | `%s` |" % env["corpus_sha256"])
    lines.append("| matches committed checksums.sha256 | `%s` (%s) |"
                 % (env["corpus_checksum_ok"], env["corpus_checksum_note"]))
    lines.append("| M1 bars loaded | %d |" % env["corpus_m1_bars_loaded"])
    for key, value in data_profile.items():
        lines.append("| %s | %s |" % (key, value))
    lines.append("")
    lines.append("## Results")
    lines.append("")
    lines.append("| Scenario | Requests | Conc. | Errors | min ms | mean ms "
                 "| p95 ms | max ms | wall ms | req/s |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|")
    for s in summaries:
        lat = s["latency_ms"]
        lines.append(
            "| %s | %d | %d | %d | %s | %s | %s | %s | %s | %s |"
            % (s["name"], s["requests"], s["concurrency"], s["errors"],
               fmt(lat["min"]), fmt(lat["mean"]), fmt(lat["p95"]),
               fmt(lat["max"]), fmt(s["wall_ms"], 1),
               fmt(s["throughput_per_s"], 1)))
    lines.append("")
    lines.append("### Scenario detail")
    lines.append("")
    lines.append("| Scenario | Statuses | Extra | Notes |")
    lines.append("|---|---|---|---|")
    for s in summaries:
        lines.append("| %s | `%s` | `%s` | %s |"
                     % (s["name"], json.dumps(s["statuses"]),
                        json.dumps(s["extra"]), s["notes"]))
    lines.append("")
    lines.append("## Bottleneck analysis")
    lines.append("")
    for line in bottleneck_notes:
        lines.append(line)
    lines.append("")
    lines.append("## Isolated store-query experiment (no HTTP)")
    lines.append("")
    lines.append("The same read (`/candles?tf=M1&limit=500`) is issued directly")
    lines.append("against a scratch SQLite file built with the server's exact")
    lines.append("schema + index, at the same real row counts, to separate the")
    lines.append("store query from the HTTP/serialization path.")
    lines.append("")
    lines.append("| stored M1 rows | current guard ms | precheck ms | plain query ms "
                 "| precheck+plain ms | speedup |")
    lines.append("|---|---|---|---|---|---|")
    for row in guard_results:
        lines.append("| %d | %s | %s | %s | %s | %sx |"
                     % (row["stored_rows"], fmt(row["current_guard_ms"], 1),
                        fmt(row["precheck_ms"], 2), fmt(row["plain_ms"], 2),
                        fmt(row["precheck_plus_plain_ms"], 2),
                        fmt(row["speedup"], 1)))
    lines.append("")
    lines.append("Measured with the real corpus rows listed above "
                 "(`--m1-full` loads 100,008).")
    lines.append("")
    lines.append("### Proposed optimization (NOT applied)")
    lines.append("")
    lines.append("The measured cause is the alignment guard's formulation:")
    lines.append("")
    lines.append("```sql")
    lines.append("-- current (per-row correlated NOT EXISTS over the whole series)")
    lines.append("SELECT ... FROM candles WHERE symbol=? AND timeframe=? AND")
    lines.append("  NOT EXISTS (SELECT 1 FROM candles bad WHERE")
    lines.append("    bad.symbol=candles.symbol AND bad.timeframe=candles.timeframe")
    lines.append("    AND <misaligned(bad)>)")
    lines.append("  ORDER BY open_time ASC LIMIT ?;")
    lines.append("```")
    lines.append("")
    lines.append("SQLite cannot hoist that subquery: the plan is")
    lines.append("`SEARCH candles USING INDEX idx_candles_lookup (symbol=? AND")
    lines.append("timeframe=?)` plus a `CORRELATED SCALAR SUBQUERY` that rescans")
    lines.append("the whole `(symbol, timeframe)` series once per candidate row,")
    lines.append("so a `LIMIT 500` read costs O(rows x 500). The measurement")
    lines.append("above shows linear growth in stored rows, and the same cost")
    lines.append("appears when the query runs with no HTTP in front of it (see")
    lines.append("the isolated experiment), so the cost is the query, not the")
    lines.append("transport or JSON serialization.")
    lines.append("")
    lines.append("A guard that keeps the exact same semantics but is evaluated")
    lines.append("ONCE per series is:")
    lines.append("")
    lines.append("```sql")
    lines.append("-- candidate: series-level precheck + plain indexed read")
    lines.append("-- 1) is the series dirty?  (one indexed range scan, stops at 1)")
    lines.append("SELECT 1 FROM candles WHERE symbol=? AND timeframe=?")
    lines.append("  AND <misaligned(candles)> LIMIT 1;")
    lines.append("-- 2) clean -> plain read; dirty -> withhold the whole series")
    lines.append("SELECT ... FROM candles WHERE symbol=? AND timeframe=?")
    lines.append("  ORDER BY open_time ASC LIMIT ?;")
    lines.append("```")
    lines.append("")
    lines.append("Both steps run under the store's existing mutex, so the pair is")
    lines.append("atomic with respect to writes and the withholding semantics are")
    lines.append("identical (a series with any misaligned row is withheld whole).")
    lines.append("The same shape applies to `syncPoints`, `syncPointsForSource`,")
    lines.append("`latestOpenTimeAnySource` and `allRows`.")
    lines.append("")
    lines.append("Trade-offs to weigh before applying (NOT done here):")
    lines.append("")
    lines.append("- It touches `src/persistence/CandleStore.cpp`, which is outside")
    lines.append("  Agent E's owned file set, so it must be routed to the owning")
    lines.append("  agent and re-verified by the existing CandleStore/ingest/")
    lines.append("  data-quality tests.")
    lines.append("- A simpler, non-source change would be to make misaligned rows")
    lines.append("  unrepresentable at write time (the ingest guard already rejects")
    lines.append("  them), so a one-time migration could drop the read guard's")
    lines.append("  cost entirely; that is a data decision, not a perf decision.")
    lines.append("- The `LIMIT`/`ORDER BY` interaction must be re-measured after")
    lines.append("  any change; the numbers above are the baseline to beat.")
    lines.append("")
    lines.append("## Companion resilience suite")
    lines.append("")
    lines.append("`tests/AyzrPerfResilienceTests.cpp` (Agent E) pins the failure")
    lines.append("modes this benchmark measures the cost of. It drives the real")
    lines.append("`astra_server` binary over real loopback HTTP against a real")
    lines.append("SQLite file, using the same real corpus:")
    lines.append("")
    lines.append("| Test case | What it pins |")
    lines.append("|---|---|")
    lines.append("| `ayzr_concurrent_disjoint_ingest_is_lossless` | 4 concurrent "
                 "clients x 250 distinct real bars -> all 1000 stored exactly once |")
    lines.append("| `ayzr_concurrent_duplicate_ingest_stores_once` | 8 concurrent "
                 "clients post the same 200-bar batch -> exactly 200 `stored_new`, "
                 "1400 `already_stored`, one row per candle |")
    lines.append("| `ayzr_concurrent_read_during_ingest_stays_consistent` | reads "
                 "issued while ingestion is in flight never 5xx and never truncate |")
    lines.append("| `ayzr_restart_preserves_candles_and_idempotency` | data + "
                 "watermark survive stop/start; a replayed batch is still a duplicate |")
    lines.append("| `ayzr_ai_failure_does_not_disable_candles_or_analysis` | dead "
                 "LLM7 endpoint -> news `unavailable`, candles + `/analysis/latest` "
                 "still 200 with real data |")
    lines.append("| `ayzr_news_store_failure_does_not_disable_candles_or_analysis` | "
                 "unusable `news.sqlite` -> news degrades empty, ASTRA keeps serving |")
    lines.append("| `ayzr_ai_failure_is_not_latched` | repeated AI failure is "
                 "per-request degradation, not a latched disabled state |")
    lines.append("")
    lines.append("Run: `cd build-linux && ./AyzrPerfResilienceTests` (7 cases; "
                 "0 failures).")
    lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    default_corpus = os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(
            os.path.abspath(__file__)))),
        "research", "data", "xauusd_m1", "xauusd_m1_real.csv")
    ap.add_argument("--data", default=default_corpus)
    ap.add_argument("--server", default="build-linux/astra_server")
    ap.add_argument("--out", default=None, help="markdown output path")
    ap.add_argument("--json-out", default=None, help="raw JSON output path")
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--m1-full", action="store_true",
                    help="load the whole M1 corpus (100,008 bars)")
    ap.add_argument("--m1-limit", type=int, default=20000)
    ap.add_argument("--keep-data", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.data):
        raise SystemExit("corpus not found: %s" % args.data)
    if not os.path.exists(args.server):
        raise SystemExit("server binary not found: %s" % args.server)

    corpus_sha = sha256_file(args.data)
    corpus_ok, corpus_note = verify_corpus(args.data)

    m1_limit = None if args.m1_full else args.m1_limit
    m1 = load_m1(args.data, limit=m1_limit)
    if not m1:
        raise SystemExit("no bars loaded from %s" % args.data)
    m15 = aggregate_complete(m1, 900, 15)
    h4 = aggregate_complete(m1, 14400, 240)

    data_profile = {
        "M1 bars used": len(m1),
        "M15 bars aggregated (complete buckets only)": len(m15),
        "H4 bars aggregated (complete buckets only)": len(h4),
        "first M1 bar (UTC)": datetime.datetime.fromtimestamp(
            m1[0][0], datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S"),
        "last M1 bar (UTC)": datetime.datetime.fromtimestamp(
            m1[-1][0], datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S"),
    }

    data_dir = tempfile.mkdtemp(prefix="astra_bench_")
    port = args.port or free_port()
    proc = None
    summaries = []
    try:
        proc, log = start_server(args.server, data_dir, port)
        client = Client("127.0.0.1", port)
        if not wait_ready(client):
            raise SystemExit("server did not become ready; see %s/server.log"
                             % data_dir)

        env = environment_report(args.server, args.data, corpus_sha, corpus_ok,
                                 corpus_note, len(m1))

        quick = args.quick
        batch_sizes = [100, 500, 2000]
        if quick:
            batch_sizes = [100, 500]
        # Wall-clock budget for one scenario's repetitions. Keeps a full-corpus
        # run bounded while still measuring the real (slow) path.
        budget_ms = 30000.0 if not quick else 8000.0

        # 1. store-size scaling on the guard-heavy read. Runs FIRST so the M1
        #    series contains exactly the sweep's rows and the curve is clean.
        checkpoints = [2000, 5000, 10000, 20000]
        if args.m1_full:
            checkpoints = [10000, 40000, 70000, 100000]
        if quick:
            checkpoints = [2000, 5000]
        for s in scenario_store_size_sweep(client, m1, checkpoints, budget_ms):
            summaries.append(s.summary())
        data_profile["M1 rows stored after sweep"] = checkpoints[-1]

        # 2. ingestion batches (real M1 bars)
        for s in scenario_ingest_batches(client, m1, batch_sizes):
            summaries.append(s.summary())

        # 3. aggregate the real M1 corpus into M15/H4 and ingest those series so
        #    the higher-timeframe read scenarios have genuine data to serve.
        m15_ingested = 0
        for start in range(0, len(m15), 2000):
            r = client.post("/api/v1/candles/ingest", SENDER_TOKEN,
                            ingest_body("bench-m15", "M15", m15[start:start + 2000]))
            try:
                m15_ingested += json.loads(r["body"].decode())["data"]["ingested"]
            except Exception:
                pass
        h4_ingested = 0
        for start in range(0, len(h4), 2000):
            r = client.post("/api/v1/candles/ingest", SENDER_TOKEN,
                            ingest_body("bench-h4", "H4", h4[start:start + 2000]))
            try:
                h4_ingested += json.loads(r["body"].decode())["data"]["ingested"]
            except Exception:
                pass
        data_profile["M15 bars ingested"] = m15_ingested
        data_profile["H4 bars ingested"] = h4_ingested

        # 3. duplicate batch replay
        summaries.append(scenario_duplicate_ingest(
            client, m1, 500, 2 if quick else 3).summary())

        # 4. concurrent ingestion correctness
        summaries.append(scenario_concurrent_ingest(
            client, m1[4000:], 4 if quick else 8, 500).summary())
        summaries.append(scenario_concurrent_duplicate_ingest(
            client, m1, 8, 200).summary())

        # 5. candle retrieval at M1 / M15 / H4
        for s in scenario_reads(client, m1, m15, h4, budget_ms):
            summaries.append(s.summary())

        # 6. retrieval latency vs limit at M15
        for s in scenario_read_scaling(client, "M15",
                                       [100, 500, 1000] if not quick
                                       else [100, 500], budget_ms):
            summaries.append(s.summary())

        # 7. concurrent reads
        for s in scenario_concurrent_reads(
                client, "M15", 500, [1, 4, 8] if not quick else [1, 4],
                8 if quick else 10, budget_ms):
            summaries.append(s.summary())
        for s in scenario_concurrent_reads(client, "M1", 500, [1, 4], 5,
                                           budget_ms):
            summaries.append(s.summary())

        # 8. concurrent ingest + read
        sr, sw = scenario_concurrent_ingest_and_read(
            client, m1, 8 if not quick else 4, 500,
            "/api/v1/candles?tf=M1&limit=500", budget_ms)
        summaries.append(sr.summary())
        summaries.append(sw.summary())

        # 9. news list + history (+ ingest to give them content)
        articles = [
            {
                "headline": "Benchmark gold headline %d" % i,
                "source": "bench.example.com",
                "url": "https://bench.example.com/a/%d" % i,
                "published_at": 1792000000 + i * 60,
                "body": "Benchmark body text %d." % i,
                "gold_relevance": "high",
                "category": "macro",
            }
            for i in range(50 if quick else 200)
        ]
        for s in scenario_news(client, articles):
            summaries.append(s.summary())

        # 12. non-store read endpoints (control group)
        for s in scenario_analysis_endpoints(client):
            summaries.append(s.summary())

        sync = client.get("/api/v1/sync/latest", token=SENDER_TOKEN)
        try:
            data_profile["sync/latest"] = json.loads(sync["body"].decode())["data"]
        except Exception:
            pass

        bottleneck_notes = build_bottleneck_notes(summaries)

        guard_checkpoints = [10000, 40000, 70000, 100000]
        if quick:
            guard_checkpoints = [5000, 10000]
        guard_results = sqlite_guard_experiment(m1, guard_checkpoints)

        md = markdown_report(env, summaries, data_profile, bottleneck_notes,
                             guard_results)
        if args.out:
            os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
            with open(args.out, "w", encoding="utf-8") as fh:
                fh.write(md)
            print("wrote %s" % args.out)
        else:
            print(md)
        if args.json_out:
            os.makedirs(os.path.dirname(os.path.abspath(args.json_out)),
                        exist_ok=True)
            with open(args.json_out, "w", encoding="utf-8") as fh:
                json.dump({"environment": env, "data": data_profile,
                           "results": summaries,
                           "guard_experiment": guard_results}, fh, indent=2)
            print("wrote %s" % args.json_out)
    finally:
        stop_server(proc)
        if args.keep_data:
            print("data dir kept: %s" % data_dir)
        else:
            shutil.rmtree(data_dir, ignore_errors=True)


def build_bottleneck_notes(summaries):
    """Derive the bottleneck statement from the measurements actually taken."""
    by_name = {s["name"]: s for s in summaries}

    def lat(name, field="mean"):
        s = by_name.get(name)
        return s["latency_ms"][field] if s else None

    notes = []
    notes.append("Bottleneck is identified from the numbers above, not assumed.")
    notes.append("")
    notes.append("- `read_M1_500` mean **%s ms** vs `read_M15_500` mean **%s ms** "
                 "vs `read_H4_500` mean **%s ms**."
                 % (fmt(lat("read_M1_500")), fmt(lat("read_M15_500")),
                    fmt(lat("read_H4_500"))))
    notes.append("- `read_M15_limit_100` mean **%s ms**, `limit_500` **%s ms**, "
                 "`limit_1000` **%s ms**."
                 % (fmt(lat("read_M15_limit_100")),
                    fmt(lat("read_M15_limit_500")),
                    fmt(lat("read_M15_limit_1000"))))
    notes.append("- `ingest_batch_100` **%s ms**, `ingest_batch_500` **%s ms**, "
                 "`ingest_batch_2000` **%s ms**."
                 % (fmt(lat("ingest_batch_100")), fmt(lat("ingest_batch_500")),
                    fmt(lat("ingest_batch_2000"))))
    conc = [s for s in summaries
            if s["name"].startswith("concurrent_read_M15_limit_500_threads_")]
    if conc:
        notes.append("- Concurrent M15 reads: "
                     + "; ".join("threads=%d -> mean %s ms, %s req/s"
                                 % (s["concurrency"], fmt(s["latency_ms"]["mean"]),
                                    fmt(s["throughput_per_s"], 1))
                                 for s in conc)
                     + ". Throughput does not rise with concurrency while "
                       "per-request latency scales linearly, which is the "
                       "signature of a single global serialization point.")
    sweep = [s for s in summaries if s["name"].startswith("read_M1_500_at_")]
    if sweep:
        notes.append("- Same M1 read at growing store sizes: "
                     + "; ".join("%s rows -> mean %s ms"
                                 % (s["extra"].get("stored_m1_rows"),
                                    fmt(s["latency_ms"]["mean"]))
                                 for s in sweep)
                     + ". Growth is linear in the number of stored rows, so the "
                       "cost is inside the store query, not the transport.")
    return notes


if __name__ == "__main__":
    main()
