#!/usr/bin/env python3
"""Mock MT5 sender for local end-to-end verification of the receiving API.

This stands in for the separate Windows MT5 Sender EXE (which is out of scope
for this project). It sends a small batch of synthetic candles to a LOCAL
server, replays it to prove idempotency, and sends one invalid row to prove
rejection. All data is clearly synthetic test data.

It refuses to run against a non-loopback host, so it can never touch production.

Usage:
    ASTRA_SENDER_TOKEN=dev-token ./build-linux/astra_server \
        --data-dir /tmp/astra-demo --port 8790 &
    python3 tools/ayzr_agents/mock_sender.py --port 8790 --token dev-token
"""

import argparse
import json
import urllib.error
import urllib.request

SYMBOL = "XAUUSD"
TIMEFRAME = "M15"
# M15 grid, aligned to the timeframe: 1792045800 % 900 == 0.
BASE_TIME = 1792045800


def post(host: str, port: int, path: str, token: str, body: dict):
    url = f"http://{host}:{port}{path}"
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST")
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return resp.status, json.loads(resp.read().decode())
    except urllib.error.HTTPError as exc:
        return exc.code, json.loads(exc.read().decode() or "{}")


def get(host: str, port: int, path: str, token: str):
    req = urllib.request.Request(f"http://{host}:{port}{path}", method="GET")
    req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req, timeout=10) as resp:
        return resp.status, json.loads(resp.read().decode())


def batch():
    return {
        "source_id": "mock-sender",
        "symbol": SYMBOL,
        "timeframe": TIMEFRAME,
        "candles": [
            {"time": BASE_TIME, "open": 2650.0, "high": 2655.0,
             "low": 2648.0, "close": 2654.0, "tick_volume": 100},
            {"time": BASE_TIME + 900, "open": 2654.0, "high": 2660.0,
             "low": 2652.0, "close": 2658.0, "tick_volume": 120},
        ],
    }


def invalid_batch():
    return {
        "source_id": "mock-sender",
        "symbol": SYMBOL,
        "timeframe": TIMEFRAME,
        "candles": [
            # high < low: must be rejected.
            {"time": BASE_TIME + 1800, "open": 2658.0, "high": 2650.0,
             "low": 2665.0, "close": 2660.0},
        ],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8790)
    ap.add_argument("--token", required=True)
    args = ap.parse_args()

    if args.host not in ("127.0.0.1", "localhost", "::1"):
        raise SystemExit("refusing to run against a non-loopback host")

    ok = True

    status, body = post(args.host, args.port, "/api/v1/candles/ingest",
                        args.token, batch())
    ingested = body.get("data", {}).get("ingested")
    print(f"[1] first send: HTTP {status}, ingested={ingested}")
    ok &= status == 200 and ingested == 2

    status, body = post(args.host, args.port, "/api/v1/candles/ingest",
                        args.token, batch())
    ingested = body.get("data", {}).get("ingested")
    dupes = body.get("data", {}).get("duplicates", [])
    print(f"[2] replay: HTTP {status}, ingested={ingested}, duplicates={len(dupes)}")
    ok &= status == 200 and ingested == 0 and len(dupes) == 2

    status, body = post(args.host, args.port, "/api/v1/candles/ingest",
                        args.token, invalid_batch())
    rejected = body.get("data", {}).get("rejected", [])
    print(f"[3] invalid OHLC: HTTP {status}, rejected={len(rejected)}")
    ok &= status == 200 and len(rejected) == 1

    status, body = get(args.host, args.port,
                       "/api/v1/sync/latest?source_id=mock-sender", args.token)
    data = body.get("data")
    points = data if isinstance(data, list) else (data or {}).get("points", [])
    print(f"[4] sync: HTTP {status}, points={len(points)}")
    ok &= status == 200

    print("RESULT:", "PASS" if ok else "FAIL")
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
