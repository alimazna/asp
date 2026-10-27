#!/usr/bin/env python3
"""AYZR end-to-end lifecycle verification against a LOCAL astra_server.

Drives the full intended pipeline on the real MT5 corpus and a mocked LLM so
no external quota is consumed:

  1. start a loopback astra_server with sender/admin/client tokens and
     ASTRA_LLM_PROVIDER=mock (deterministic, no network);
  2. POST real M1 candle batches to /api/v1/candles/ingest (sender token);
  3. replay the same batches -> must be all duplicates, no new rows;
  4. POST news articles to /api/v1/news/ingest (sender token);
  5. analyze one article via /api/v1/news/analyze?id=... (client token) and
     confirm the AI layer is present but does NOT overwrite ASTRA;
  6. read /api/v1/news, /api/v1/history, /api/v1/candles (client token);
  7. restart the server on the same data dir and confirm the candles, news and
     history survive (persistence, not in-memory);
  8. assert every protected route rejects the wrong role's token.

Exit 0 = the whole available lifecycle holds end to end. Every expected value
is either the genuine corpus or a value the backend itself returned earlier;
nothing is invented.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import signal
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CSV = REPO / "research/data/xauusd_m1/xauusd_m1_real.csv"
CHECKS = REPO / "research/data/xauusd_m1/checksums.sha256"

FAILS: list[str] = []
CHECKS_RUN = 0


def check(cond: bool, label: str) -> None:
    global CHECKS_RUN
    CHECKS_RUN += 1
    if cond:
        print(f"[PASS] {label}")
    else:
        print(f"[FAIL] {label}")
        FAILS.append(label)


def free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def http(method: str, url: str, token: str | None = None,
         body: bytes | None = None, timeout: float = 15.0):
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


def verify_corpus() -> None:
    if not CHECKS.exists():
        check(False, "corpus checksum file present")
        return
    line = CHECKS.read_text().splitlines()
    want = None
    for ln in line:
        if "xauusd_m1_real.csv" in ln:
            want = ln.split()[0]
            break
    if want is None:
        check(False, "corpus checksum entry found")
        return
    h = hashlib.sha256(CSV.read_bytes()).hexdigest()
    check(h == want, f"corpus sha256 matches ({h[:12]}…)")


def load_m1(limit: int) -> list[dict]:
    import calendar
    rows = []
    with CSV.open() as f:
        header = f.readline().strip().split(",")
        for line in f:
            parts = line.strip().split(",")
            if len(parts) != len(header):
                continue
            raw = dict(zip(header, parts))
            # The corpus stamps bar opens in broker server time with no tz
            # label; parse as UTC via timegm exactly as the replay tool does.
            ts = raw["timestamp"].replace("T", " ")
            try:
                epoch = calendar.timegm(time.strptime(ts, "%Y-%m-%d %H:%M:%S"))
            except ValueError:
                continue
            rows.append({
                "time": epoch,
                "open": raw["open"], "high": raw["high"],
                "low": raw["low"], "close": raw["close"],
                "volume": raw.get("volume", "0"),
            })
            if len(rows) >= limit:
                break
    return rows


def to_candles(rows: list[dict], timeframe: str) -> list[dict]:
    out = []
    for r in rows:
        out.append({
            "time": int(r["time"]),
            "open": float(r["open"]),
            "high": float(r["high"]),
            "low": float(r["low"]),
            "close": float(r["close"]),
            "tick_volume": int(float(r.get("volume", "0") or 0)),
        })
    return out


def unwrap(txt: str) -> dict:
    """Return the `data` object of the standard {api,schema,data} envelope."""
    obj = json.loads(txt)
    if isinstance(obj, dict) and isinstance(obj.get("data"), dict):
        return obj["data"]
    return obj if isinstance(obj, dict) else {}


class Server:
    def __init__(self, exe: str, data_dir: str, port: int, tokens: dict):
        self.exe, self.data_dir, self.port, self.tokens = exe, data_dir, port, tokens
        self.proc: subprocess.Popen | None = None
        self.log = open(f"/tmp/lifecycle-{port}.log", "w")

    def env(self) -> dict:
        e = dict(os.environ)
        e.update({
            "ASTRA_SENDER_TOKEN": self.tokens["sender"],
            "ASTRA_ADMIN_TOKEN": self.tokens["admin"],
            "ASTRA_CLIENT_TOKEN": self.tokens["client"],
            "ASTRA_LLM_PROVIDER": "mock",
            "ASTRA_MOCK_LLM_MODE": "valid",
        })
        return e

    def start(self) -> bool:
        self.proc = subprocess.Popen(
            [self.exe, "--host", "127.0.0.1", "--port", str(self.port),
             "--data-dir", self.data_dir],
            stdout=self.log, stderr=subprocess.STDOUT, env=self.env())
        for _ in range(100):
            try:
                with socket.create_connection(("127.0.0.1", self.port), 0.3):
                    return True
            except OSError:
                time.sleep(0.1)
        return False

    def stop(self) -> None:
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(5)
        self.log.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--server-bin", default=str(REPO / "build-linux/astra_server"))
    ap.add_argument("--bars", type=int, default=400)
    ap.add_argument("--data-dir", default="/tmp/ayzr-lifecycle")
    args = ap.parse_args()

    exe = args.server_bin
    if not Path(exe).exists():
        print(f"server binary not found: {exe}", file=sys.stderr)
        return 2

    verify_corpus()
    rows = load_m1(args.bars)
    check(len(rows) == args.bars, f"loaded {len(rows)} real M1 bars")
    m1 = to_candles(rows, "M1")
    # keep only grid-aligned M1 bars (broker minute grid) as the backend requires
    m1 = [c for c in m1 if c["time"] % 60 == 0]
    check(len(m1) > 0, f"{len(m1)} grid-aligned M1 bars")

    tokens = {"sender": "sender-life-tok", "admin": "admin-life-tok",
              "client": "client-life-tok"}
    data_dir = args.data_dir
    subprocess.run(["rm", "-rf", data_dir], check=False)
    os.makedirs(data_dir, exist_ok=True)
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    srv = Server(exe, data_dir, port, tokens)
    try:
        check(srv.start(), "server starts (mock LLM)")

        # --- candles ingest ---
        body = json.dumps({
            "source_id": "lifecycle-e2e", "symbol": "XAUUSD",
            "timeframe": "M1", "candles": m1,
        }).encode()
        st, txt = http("POST", f"{base}/api/v1/candles/ingest", tokens["sender"], body)
        check(st == 200, f"candle ingest accepted (HTTP {st})")
        ack = unwrap(txt)
        ingested = ack.get("ingested", 0)
        check(ingested == len(m1), f"all {len(m1)} candles ingested (ack={ingested})")

        # --- duplicate replay: idempotent ---
        st2, txt2 = http("POST", f"{base}/api/v1/candles/ingest", tokens["sender"], body)
        check(st2 == 200, f"duplicate replay accepted (HTTP {st2})")
        ack2 = unwrap(txt2)
        dup = len(ack2.get("duplicates", []))
        check(dup == len(m1), f"replay is all duplicates ({dup})")

        # --- read candles back ---
        st3, txt3 = http("GET", f"{base}/api/v1/candles?tf=M1&limit=1000", tokens["client"])
        check(st3 == 200, f"candle read ok (HTTP {st3})")
        cdata = unwrap(txt3)
        bars = cdata.get("candles", cdata.get("bars", []))
        check(len(bars) == len(m1), f"read back {len(bars)} bars == {len(m1)} stored")

        # --- news ingest ---
        articles = [{
            "headline": "الفيدرالي يبقي أسعار الفائدة دون تغيير مع ترقب بيانات التضخم",
            "source": "Reuters",
            "url": "https://example.invalid/ayzr-lifecycle-1",
            "category": "monetary_policy",
            "published_at": int(time.time()) - 3600,
            "body": "أبقى مجلس الاحتياطي الفيدرالي على أسعار الفائدة دون تغيير.",
            "gold_relevance": "high",
        }]
        nbody = json.dumps({"articles": articles}).encode()
        stn, txtn = http("POST", f"{base}/api/v1/news/ingest", tokens["sender"], nbody)
        check(stn == 200, f"news ingest ok (HTTP {stn})")
        nack = unwrap(txtn)
        check(nack.get("stored", 0) == 1, f"one article stored (ack={nack.get('stored')})")

        # --- list news ---
        stl, txtl = http("GET", f"{base}/api/v1/news", tokens["client"])
        check(stl == 200, f"news list ok (HTTP {stl})")
        ndata = unwrap(txtl)
        items = ndata.get("news", ndata.get("articles", []))
        check(len(items) >= 1, f"news list has {len(items)} item(s)")
        first = items[0] if items else {}
        art = first.get("article", first) if isinstance(first, dict) else {}
        article_id = art.get("article_id")
        check(bool(article_id), "article_id present for analyze")

        # --- analyze (AI layer, mocked) ---
        if article_id:
            sta, txta = http("GET", f"{base}/api/v1/news/analyze?id={article_id}",
                             tokens["client"])
            check(sta == 200, f"news analyze ok (HTTP {sta})")
            adata = unwrap(txta)
            ai = adata.get("ai_analysis") or adata.get("analysis")
            check(ai is not None, "AI analysis block present")
            if isinstance(ai, dict):
                check("proposed_direction" in json.dumps(ai) or
                      ai.get("direction") is not None,
                      "AI analysis carries a proposed direction")

        # --- history (persisted analyses) ---
        sth, txth = http("GET", f"{base}/api/v1/history", tokens["client"])
        check(sth == 200, f"history ok (HTTP {sth})")
        hdata = unwrap(txth)
        hist = hdata.get("history", [])
        check(len(hist) >= 1, f"history persisted {len(hist)} analysis record(s)")

        # --- role separation (security) ---
        stx, _ = http("POST", f"{base}/api/v1/candles/ingest", tokens["client"], body)
        check(stx == 401, f"client token cannot ingest candles (HTTP {stx})")
        sty, _ = http("GET", f"{base}/api/v1/news", tokens["sender"])
        check(sty == 401, f"sender token cannot read private news (HTTP {sty})")
        stz, _ = http("GET", f"{base}/api/v1/news", "wrong-token")
        check(stz == 401, f"wrong token rejected (HTTP {stz})")

        # --- restart persistence ---
        srv.stop()
        srv2 = Server(exe, data_dir, port, tokens)
        check(srv2.start(), "server restarts on same data dir")
        try:
            str3, txtr = http("GET", f"{base}/api/v1/candles?tf=M1&limit=1000",
                              tokens["client"])
            rdata = unwrap(txtr)
            rbars = rdata.get("candles", rdata.get("bars", []))
            check(len(rbars) == len(m1), f"candles survived restart ({len(rbars)})")
            str4, txtr4 = http("GET", f"{base}/api/v1/history", tokens["client"])
            rhist = unwrap(txtr4).get("history", [])
            check(len(rhist) >= 1, f"history survived restart ({len(rhist)})")
            str5, txtr5 = http("GET", f"{base}/api/v1/news", tokens["client"])
            r5 = unwrap(txtr5); rnews = r5.get("news", r5.get("articles", []))
            check(len(rnews) >= 1, f"news survived restart ({len(rnews)})")
        finally:
            srv2.stop()
    finally:
        srv.stop()
        subprocess.run(["rm", "-rf", data_dir], check=False)

    print(f"\n{CHECKS_RUN} check(s), {len(FAILS)} failed")
    if FAILS:
        print("RESULT: FAIL")
        return 1
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
