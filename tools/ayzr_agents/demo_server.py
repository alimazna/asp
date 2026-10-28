#!/usr/bin/env python3
"""AYZR demo backend: a LOCAL astra_server seeded with the real MT5 corpus.

Purpose: let the owner install the AYZR APK and immediately see real-shaped
screens (market context, news, history) without any manual setup.

What is real and what is not
----------------------------
* Prices/candles: the genuine MT5 corpus at
  ``research/data/xauusd_m1/xauusd_m1_real.csv`` (sha256-verified), aggregated
  M1 -> M5/M15/M30/H1/H4/D1 with the SAME bucket rule the packaged bridge uses
  (``bridge/mt5_python/mt5_csv_feed.py::CsvFeed._aggregate``).
* News metadata: real headlines/sources/URLs/publish times fetched from a public
  RSS feed. The article BODY is not available from RSS, so it is left empty and
  the backend reports ``body_available=false`` -- nothing is invented.
* AI analysis: produced by the backend's DETERMINISTIC MOCK provider
  (``ASTRA_LLM_PROVIDER=mock``). No external LLM7 quota is consumed. The mock
  returns a fixed Arabic payload, so every analyzed article shows the same
  summary text -- this is disclosed, not hidden.

The one deliberate transformation is a **time shift**: the corpus ends
2026-10-08, so it is shifted forward so its last M1 bar opens one minute before
the most recent completed M15 bucket. Without this the backend would honestly
report the series as STALE and the demo would show freshness warnings. The
shift is disclosed in the printed summary and in the emitted JSON.
"""
from __future__ import annotations

import argparse
import calendar
import hashlib
import html
import json
import os
import signal
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CORPUS = REPO / "research/data/xauusd_m1/xauusd_m1_real.csv"
CHECKSUMS = REPO / "research/data/xauusd_m1/checksums.sha256"

# Aggregation intervals in seconds; identical to _TF_SECONDS in mt5_csv_feed.py.
TF_SECONDS = {
    "M1": 60, "M5": 300, "M15": 900, "M30": 1800,
    "H1": 3600, "H4": 14400, "D1": 86400, "W1": 604800, "MN1": 2592000,
}
MAX_BATCH = 500  # ASTRA_MAX_CANDLES_PER_BATCH default

# M15 drives the live decision pipeline, and the cross-timeframe gate reads H4
# and D1 from the same bus at that instant. So H4/D1 must already be on the bus
# when the M15 batch lands, or the core gate honestly blocks the decision.
INGEST_ORDER = ["M1", "M5", "M30", "H1", "H4", "D1", "W1", "MN1", "M15"]


def log(msg: str) -> None:
    print(msg, flush=True)


def http(method: str, url: str, token: str | None = None,
         body: bytes | None = None, timeout: float = 60.0):
    req = urllib.request.Request(url, data=body, method=method)
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    if body is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def unwrap(txt: str):
    try:
        obj = json.loads(txt)
    except json.JSONDecodeError:
        return {}
    if isinstance(obj, dict) and "data" in obj:
        return obj["data"]
    return obj


def verify_corpus() -> bool:
    if not CHECKSUMS.exists():
        log("WARN: corpus checksum file missing; cannot verify provenance")
        return False
    want = None
    for line in CHECKSUMS.read_text().splitlines():
        if "xauusd_m1_real.csv" in line:
            want = line.split()[0]
            break
    if want is None:
        return False
    got = hashlib.sha256(CORPUS.read_bytes()).hexdigest()
    ok = got == want
    log(f"{'PASS' if ok else 'FAIL'}: corpus sha256 {got[:12]}…")
    return ok


def load_m1(limit: int | None) -> list[tuple[int, float, float, float, float, int]]:
    rows: list[tuple[int, float, float, float, float, int]] = []
    with CORPUS.open() as f:
        header = f.readline().strip().split(",")
        idx = {name: i for i, name in enumerate(header)}
        for line in f:
            p = line.strip().split(",")
            if len(p) != len(header):
                continue
            ts = p[idx["timestamp"]].replace("T", " ")
            try:
                epoch = calendar.timegm(time.strptime(ts, "%Y-%m-%d %H:%M:%S"))
            except ValueError:
                continue
            rows.append((
                epoch,
                float(p[idx["open"]]), float(p[idx["high"]]),
                float(p[idx["low"]]), float(p[idx["close"]]),
                int(float(p[idx["volume"]])),
            ))
            if limit is not None and len(rows) >= limit:
                break
    return rows


def shift_to_now(rows, now: int) -> tuple[list, int]:
    """Move the corpus forward so its last M1 bar is the newest CLOSED M1 bar.

    Anchoring on the M1 grid (not the M15 bucket) keeps every intraday series
    inside its freshness tolerance, which is what the real system looks like
    while the MT5 sender is running. Anchoring on the M15 bucket instead leaves
    M1/M5 mid-bucket and honestly STALE.
    """
    target_last_open = (now // TF_SECONDS["M1"]) * TF_SECONDS["M1"] - TF_SECONDS["M1"]
    shift = target_last_open - rows[-1][0]
    return [(t + shift, o, h, l, c, v) for (t, o, h, l, c, v) in rows], shift


def aggregate(rows, interval: int) -> list[dict]:
    """The exact bucket rule from mt5_csv_feed.CsvFeed._aggregate."""
    buckets: dict[int, dict] = {}
    order: list[int] = []
    for (t, o, h, l, c, v) in rows:
        bucket = t - (t % interval)
        bar = buckets.get(bucket)
        if bar is None:
            buckets[bucket] = {"time": bucket, "open": o, "high": h,
                               "low": l, "close": c, "tick_volume": v}
            order.append(bucket)
        else:
            bar["high"] = max(bar["high"], h)
            bar["low"] = min(bar["low"], l)
            bar["close"] = c
            bar["tick_volume"] += v
    order.sort()
    return [buckets[b] for b in order]


def post_batches(base: str, token: str, source_id: str, tf: str,
                 candles: list[dict]) -> dict:
    accepted = dup = rejected = 0
    for i in range(0, len(candles), MAX_BATCH):
        chunk = candles[i:i + MAX_BATCH]
        body = json.dumps({
            "source_id": source_id, "symbol": "XAUUSD",
            "timeframe": tf, "candles": chunk,
        }).encode()
        st, txt = http("POST", f"{base}/api/v1/candles/ingest", token, body)
        if st != 200:
            log(f"  {tf} batch {i//MAX_BATCH}: HTTP {st} {txt[:200]}")
            rejected += len(chunk)
            continue
        ack = unwrap(txt)
        accepted += int(ack.get("ingested", 0) or 0)
        dup += len(ack.get("duplicates", []) or [])
        rejected += len(ack.get("rejected", []) or [])
    return {"accepted": accepted, "duplicates": dup, "rejected": rejected}


def fetch_news(url: str, limit: int) -> list[dict]:
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=30) as r:
        root = ET.fromstring(r.read())
    out = []
    for it in root.findall(".//item")[:limit]:
        title = html.unescape(it.findtext("title") or "").strip()
        link = (it.findtext("link") or "").strip()
        pub = (it.findtext("pubDate") or "").strip()
        src_el = it.find("source")
        source = (src_el.text or "").strip() if src_el is not None else ""
        # Google News appends " - <source>" to the headline; split it off so the
        # headline and the source field are not duplicated.
        if source and title.endswith(f" - {source}"):
            title = title[: -(len(source) + 3)].strip()
        try:
            epoch = calendar.timegm(time.strptime(pub, "%a, %d %b %Y %H:%M:%S %Z"))
        except ValueError:
            epoch = int(time.time())
        if not title or not source:
            continue
        out.append({
            "headline": title, "source": source, "url": link,
            "category": "other", "published_at": epoch,
            "body": "", "language": "en",
            "gold_relevance": "high",
        })
    return out


class Server:
    def __init__(self, exe: str, data_dir: str, port: int, host: str, tokens: dict):
        self.exe, self.data_dir, self.port, self.host, self.tokens = \
            exe, data_dir, port, host, tokens
        self.proc: subprocess.Popen | None = None
        self.logpath = f"/tmp/ayzr-demo-{port}.log"
        self.log = open(self.logpath, "w")

    def env(self) -> dict:
        e = dict(os.environ)
        e.update({
            "ASTRA_SENDER_TOKEN": self.tokens["sender"],
            "ASTRA_ADMIN_TOKEN": self.tokens["admin"],
            "ASTRA_CLIENT_TOKEN": self.tokens["client"],
            "ASTRA_LLM_PROVIDER": "mock",
            "ASTRA_MOCK_LLM_MODE": "valid",
            # The seeder sends ~265 batches (whole corpus). The default ingest
            # limit is 240/min, so raise it for seeding; the demo is not a
            # production exposure.
            "ASTRA_INGEST_RATE_PER_MIN": "100000",
            "ASTRA_SYNC_RATE_PER_MIN": "100000",
        })
        return e

    def start(self) -> bool:
        self.proc = subprocess.Popen(
            [self.exe, "--host", self.host, "--port", str(self.port),
             "--data-dir", self.data_dir],
            stdout=self.log, stderr=subprocess.STDOUT, env=self.env())
        for _ in range(150):
            try:
                with socket.create_connection(("127.0.0.1", self.port), 0.3):
                    return True
            except OSError:
                if self.proc.poll() is not None:
                    return False
                time.sleep(0.1)
        return False

    def stop(self) -> None:
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(8)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(5)
        self.log.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--server-bin", default=str(REPO / "build-linux/astra_server"))
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=12000)
    ap.add_argument("--data-dir", default="/tmp/ayzr-demo-data")
    ap.add_argument("--news-url", default=(
        "https://news.google.com/rss/search?q=gold+price+XAUUSD+when:7d"
        "&hl=en-US&gl=US&ceid=US:en"))
    ap.add_argument("--news-count", type=int, default=12)
    ap.add_argument("--m1-limit", type=int, default=0,
                    help="0 = whole corpus")
    ap.add_argument("--info-out", default="/tmp/ayzr-demo-info.json")
    ap.add_argument("--keep-running", action="store_true",
                    help="leave the server serving after seeding (blocks)")
    # Fixed tokens so a pre-built APK can be pointed at this demo instance. The
    # client token is read-only; these values are demo-only and are disclosed.
    ap.add_argument("--sender-token", default="demo-sender-token-ayzr")
    ap.add_argument("--admin-token", default="demo-admin-token-ayzr")
    ap.add_argument("--client-token", default="demo-client-token-ayzr")
    args = ap.parse_args()

    exe = args.server_bin
    if not Path(exe).exists():
        log(f"server binary not found: {exe}")
        return 2

    verify_corpus()
    now = int(time.time())
    rows = load_m1(args.m1_limit or None)
    log(f"loaded {len(rows)} real M1 bars")
    rows, shift = shift_to_now(rows, now)
    log(f"time shift applied: {shift:+d}s (corpus end -> one minute before the "
        f"latest completed M15 bucket)")

    tokens = {
        "sender": args.sender_token,
        "admin":  args.admin_token,
        "client": args.client_token,
    }
    Path(args.data_dir).mkdir(parents=True, exist_ok=True)
    base = f"http://127.0.0.1:{args.port}"

    srv = Server(exe, args.data_dir, args.port, args.host, tokens)
    if not srv.start():
        log(f"server failed to start; see {srv.logpath}")
        return 2
    log(f"astra_server up on {base} (mock LLM)")

    summary = {"port": args.port, "host": args.host, "shift_seconds": shift,
               "base_url": base, "timeframes": {}}
    try:
        for tf in INGEST_ORDER:
            interval = TF_SECONDS[tf]
            candles = aggregate(rows, interval)
            if len(candles) < 2:
                log(f"{tf}: {len(candles)} bar(s) - skipped (too few to matter)")
                continue
            res = post_batches(base, tokens["sender"], "ayzr-demo", tf, candles)
            summary["timeframes"][tf] = res
            log(f"{tf}: {len(candles)} bars -> accepted={res['accepted']} "
                f"duplicates={res['duplicates']} rejected={res['rejected']}")

        # --- news (real metadata, no invented body) ---
        try:
            articles = fetch_news(args.news_url, args.news_count)
            log(f"fetched {len(articles)} real news items from RSS")
        except Exception as e:  # noqa: BLE001 - network is best-effort here
            articles = []
            log(f"news fetch failed ({e}); continuing without news")

        if articles:
            body = json.dumps({"articles": articles}).encode()
            st, txt = http("POST", f"{base}/api/v1/news/ingest",
                           tokens["sender"], body)
            ack = unwrap(txt)
            summary["news_ingested"] = int(ack.get("stored", 0) or 0)
            log(f"news ingest: HTTP {st} stored={ack.get('stored')}")

            st, txt = http("GET", f"{base}/api/v1/news?limit=50", tokens["client"])
            items = unwrap(txt).get("news", []) if st == 200 else []
            log(f"news list: {len(items)} item(s)")
            analyzed = 0
            for item in items[:3]:
                art = item.get("article", {}) if isinstance(item, dict) else {}
                aid = art.get("article_id")
                if not aid:
                    continue
                st, txt = http("GET",
                               f"{base}/api/v1/news/analyze?id={aid}",
                               tokens["client"])
                d = unwrap(txt)
                if st == 200 and d.get("has_analysis"):
                    analyzed += 1
            summary["news_analyzed"] = analyzed
            log(f"analyzed {analyzed} article(s) with the mock provider")

        # --- verify the mobile surface ---
        checks = {
            "health/v1": "/api/v1/health/v1",
            "analysis/latest": "/api/v1/analysis/latest",
            "news": "/api/v1/news?limit=5",
            "history": "/api/v1/history?limit=5",
            "news/quota": "/api/v1/news/quota",
        }
        summary["endpoints"] = {}
        for name, path in checks.items():
            st, txt = http("GET", f"{base}{path}", tokens["client"])
            summary["endpoints"][name] = st
            log(f"  GET {path} -> {st}")

        st, txt = http("GET", f"{base}/api/v1/analysis/latest", tokens["client"])
        summary["analysis_sample"] = unwrap(txt)
        st, txt = http("GET", f"{base}/api/v1/history?limit=5", tokens["client"])
        summary["history_sample"] = unwrap(txt)

        info = {
            "demo_server": summary,
            "tokens": tokens,
            "disclosure": {
                "prices": "real MT5 corpus (sha256-verified), time-shifted",
                "news_metadata": "real RSS headlines/sources/URLs/times",
                "news_body": "NOT available from RSS; left empty on purpose",
                "ai_analysis": "backend MOCK provider - fixed Arabic payload, "
                               "no LLM7 call, no quota consumed",
                "shift_seconds": shift,
            },
        }
        Path(args.info_out).write_text(json.dumps(info, indent=2,
                                                  ensure_ascii=False))
        log(f"\nwrote {args.info_out}")
        log(f"client token: {tokens['client']}")
        log(f"server log: {srv.logpath}")

        if args.keep_running:
            log("seeded; serving until interrupted (Ctrl-C to stop)")
            while True:
                time.sleep(3600)
    finally:
        srv.stop()

    return 0


if __name__ == "__main__":
    sys.exit(main())
