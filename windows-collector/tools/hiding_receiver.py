#!/usr/bin/env python3
"""A test receiver that deliberately hides chosen timeframes.

Purpose: prove the collector's "do not resend what the receiver already holds"
logic. The collector decides what to send with

    stream.to_send = sorted(set(local) - have_remote)

so the only way to prove it honours a receiver's inventory is to make a
receiver whose inventory is deliberately incomplete. This server accepts and
persists candles for every timeframe, but its ``GET /candles`` answer reports an
empty list for the timeframes named with ``--hide``.

Expected result against a collector that has already delivered all nine
timeframes: the hidden timeframes are re-sent, the visible ones are not.

This speaks the real wire contract, taken from the collector's own client
(``collector/receiver.py``) and the ASTRA C++ server's responses, not a
convenient approximation of it:

    GET  /health          no auth, liveness
    GET  /sync/latest     bearer auth, credential probe
    GET  /candles         bearer auth, ?symbol=&timeframe=&limit=
                          -> {"data": {"bars": [{"time": <epoch>, ...}]}}
    POST /candles/ingest  bearer auth
                          -> {"data": {"accepted": [{"identity": ..., "state":
                             "stored_new"|"already_stored"}], "duplicates": [],
                             "rejected": [], "ingested": <n>}}

TEST SERVER ONLY. It is not the ASTRA backend and must never be used as one.
It stores everything in memory, so state is lost when it exits.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, List, Optional, Set, Tuple
from urllib.parse import parse_qs, urlparse

#: Identity format the collector's client can translate back to its own uid.
IDENTITY = "symbol={symbol}&timeframe={timeframe}&open_time={open_time}"


class Store:
    """In-memory candle inventory, keyed by the same identity the real server
    uses. Deliberately not a database: this is a test double."""

    def __init__(self, token: str, hide: Set[str]):
        self.lock = threading.Lock()
        self.token = token
        self.hide = {t.upper() for t in hide}
        #: (symbol, timeframe, open_time) -> candle payload
        self.candles: Dict[Tuple[str, str, int], Dict[str, Any]] = {}
        self.stats = {"requests": 0, "ingested_new": 0, "ingested_dup": 0,
                      "rejected": 0, "hidden_queries": 0}
        self.log: List[str] = []

    def note(self, message: str) -> None:
        self.log.append(message)
        print(message, flush=True)


def _parse_candle(raw: Any, symbol: str, timeframe: str) -> Optional[Dict[str, Any]]:
    """Validate one candle the way the real server does, and return the stored
    form. Returns None when the record must be rejected.

    The open time arrives under ``time`` (see StoredCandle.to_payload). A
    payload that omits it cannot be identified and is rejected rather than
    stored under a guessed timestamp.
    """
    if not isinstance(raw, dict):
        return None
    value = raw.get("time", raw.get("open_time_utc"))
    if value is None:
        return None
    try:
        open_time = int(value)
        o = float(raw["open"])
        h = float(raw["high"])
        lo = float(raw["low"])
        c = float(raw["close"])
    except (KeyError, TypeError, ValueError):
        return None
    # OHLC ordering, the same guard the C++ server applies.
    if h < lo or h < o or h < c or lo > o or lo > c:
        return None
    return {
        "symbol": symbol,
        "timeframe": timeframe,
        "time": open_time,
        "open": o,
        "high": h,
        "low": lo,
        "close": c,
        "tick_volume": int(raw.get("tick_volume", 0) or 0),
        "spread": int(raw.get("spread", 0) or 0),
        "real_volume": int(raw.get("real_volume", 0) or 0),
        "uid": str(raw.get("uid", "")),
    }


class Handler(BaseHTTPRequestHandler):
    store: Store = None  # type: ignore

    protocol_version = "HTTP/1.1"

    def log_message(self, *args) -> None:
        pass  # the store prints what matters

    # -- helpers ------------------------------------------------------------

    def _authorized(self) -> bool:
        return self.headers.get("Authorization", "") == "Bearer " + self.store.token

    @staticmethod
    def _route(path: str) -> str:
        """Strip the API prefix the real server mounts its routes under.

        The collector builds URLs as ``base_url + path`` where ``base_url``
        already ends in ``/api/v1``, so requests arrive as
        ``/api/v1/candles/ingest``. Accepting the bare path as well keeps curl
        checks convenient.
        """
        for prefix in ("/api/v1",):
            if path.startswith(prefix):
                return path[len(prefix):] or "/"
        return path

    def _reply(self, status: int, payload: Dict[str, Any]) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _unauthorized(self) -> None:
        self._reply(401, {"error": "true", "code": "unauthorized",
                          "message": "a valid sender bearer token is required"})

    def _read_body(self) -> Optional[Dict[str, Any]]:
        length = int(self.headers.get("Content-Length", 0) or 0)
        if length <= 0:
            return None
        raw = self.rfile.read(length)
        try:
            parsed = json.loads(raw.decode("utf-8", "replace"))
        except json.JSONDecodeError:
            return None
        return parsed if isinstance(parsed, dict) else None

    # -- routes -------------------------------------------------------------

    def do_GET(self) -> None:  # noqa: N802
        path = self._route(urlparse(self.path).path)
        self.store.stats["requests"] += 1

        if path == "/health":
            self._reply(200, {"api": "v1", "schema": "1.0",
                              "data": {"aggregate": "OK",
                                       "decision_grade_data": True,
                                       "degraded_reasons": [],
                                       "observed_at": 0,
                                       "version": "v1",
                                       "strategy_version": "test-receiver-1"}})
            return

        if not self._authorized():
            self._unauthorized()
            return

        if path == "/sync/latest":
            with self.store.lock:
                newest = None
                if self.store.candles:
                    newest = max(self.store.candles.values(),
                                 key=lambda c: c["time"])
            self._reply(200, {"api": "v1", "schema": "1.0",
                              "data": {"latest": newest}})
            return

        if path == "/candles":
            query = parse_qs(urlparse(self.path).query)
            symbol = (query.get("symbol") or [""])[0]
            timeframe = (query.get("timeframe") or [""])[0].upper()
            limit = int((query.get("limit") or ["0"])[0] or 0)

            # The point of this server: report nothing for a hidden stream,
            # while still accepting and storing everything sent to it.
            if timeframe in self.store.hide:
                with self.store.lock:
                    self.store.stats["hidden_queries"] += 1
                    held = sum(1 for (s, t, _) in self.store.candles
                               if s == symbol and t == timeframe)
                self.store.note(f"GET /candles {symbol} {timeframe} -> "
                                f"REPORTED EMPTY (holding {held}, hidden)")
                self._reply(200, {"api": "v1", "schema": "1.0",
                                  "data": {"bars": [], "symbol": symbol,
                                           "timeframe": timeframe, "count": 0,
                                           "closed_only": True,
                                           "newest_closed_time": 0}})
                return

            with self.store.lock:
                rows = [dict(c) for (s, t, _), c in self.store.candles.items()
                        if (not symbol or s == symbol)
                        and (not timeframe or t == timeframe)]
            rows.sort(key=lambda c: c["time"])
            if limit > 0:
                rows = rows[-limit:]
            bars = [{"time": c["time"], "open": c["open"], "high": c["high"],
                     "low": c["low"], "close": c["close"],
                     "tick_volume": c["tick_volume"], "spread": c["spread"],
                     "real_volume": c["real_volume"]} for c in rows]
            self.store.note(f"GET /candles {symbol} {timeframe} -> "
                            f"{len(bars)} bar(s)")
            self._reply(200, {"api": "v1", "schema": "1.0",
                              "data": {"bars": bars, "symbol": symbol,
                                       "timeframe": timeframe,
                                       "count": len(bars),
                                       "closed_only": True,
                                       "newest_closed_time":
                                           bars[-1]["time"] if bars else 0}})
            return

        self._reply(404, {"error": "true", "code": "not_found"})

    def do_POST(self) -> None:  # noqa: N802
        path = self._route(urlparse(self.path).path)
        self.store.stats["requests"] += 1

        if not self._authorized():
            self._unauthorized()
            return

        if path != "/candles/ingest":
            self._reply(404, {"error": "true", "code": "not_found"})
            return

        body = self._read_body()
        if body is None:
            self._reply(400, {"error": "true", "code": "bad_request",
                              "message": "body must be a JSON object"})
            return

        symbol = str(body.get("symbol") or "")
        timeframe = str(body.get("timeframe") or "").upper()
        source_id = str(body.get("source_id") or "")
        incoming = body.get("candles")
        if not source_id or not symbol or not timeframe \
                or not isinstance(incoming, list):
            self._reply(400, {"error": "true", "code": "missing_fields",
                              "message": "source_id, symbol, timeframe and "
                                         "candles are required"})
            return

        accepted: List[Dict[str, Any]] = []
        duplicates: List[Dict[str, Any]] = []
        rejected: List[Dict[str, Any]] = []
        ingested = 0

        with self.store.lock:
            for raw in incoming:
                parsed = _parse_candle(raw, symbol, timeframe)
                if parsed is None:
                    rejected.append({"state": "rejected",
                                     "reason": "candle rejected: malformed or "
                                               "OHLC ordering violated"})
                    self.store.stats["rejected"] += 1
                    continue
                key = (symbol, timeframe, parsed["time"])
                identity = IDENTITY.format(symbol=symbol, timeframe=timeframe,
                                           open_time=parsed["time"])
                if key in self.store.candles:
                    # Same candle, second time. The real server answers
                    # `already_stored` and counts nothing as ingested.
                    duplicates.append({"identity": identity,
                                       "state": "already_stored"})
                    self.store.stats["ingested_dup"] += 1
                else:
                    self.store.candles[key] = parsed
                    accepted.append({"identity": identity,
                                     "state": "stored_new"})
                    ingested += 1
                    self.store.stats["ingested_new"] += 1

        hidden = " [HIDDEN STREAM]" if timeframe in self.store.hide else ""
        self.store.note(
            f"POST /candles/ingest {symbol} {timeframe}{hidden} "
            f"sent={len(incoming)} new={len(accepted)} "
            f"dup={len(duplicates)} rejected={len(rejected)}")

        self._reply(200, {"api": "v1", "schema": "1.0",
                          "data": {"source_id": source_id, "symbol": symbol,
                                   "timeframe": timeframe,
                                   "accepted": accepted,
                                   "duplicates": duplicates,
                                   "rejected": rejected,
                                   "ingested": ingested}})


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Test receiver that hides chosen timeframes from /candles.")
    parser.add_argument("--port", type=int, default=12000)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--token", required=True,
                        help="bearer token the collector must present")
    parser.add_argument("--hide", default="M15,M5,M1",
                        help="comma-separated timeframes to report as empty "
                             "(default: M15,M5,M1)")
    args = parser.parse_args(argv)

    hide = {t.strip().upper() for t in args.hide.split(",") if t.strip()}
    store = Store(token=args.token, hide=hide)
    Handler.store = store

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"test receiver listening on http://{args.host}:{args.port}", flush=True)
    print(f"token required      : <the one passed with --token>", flush=True)
    print(f"timeframes REPORTED EMPTY (but still stored): "
          f"{', '.join(sorted(hide)) or '(none)'}", flush=True)
    print("GET /candles will answer an empty list for those streams.", flush=True)
    print("Press Ctrl+C to stop.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping", flush=True)
    finally:
        server.server_close()
        s = store.stats
        print(f"\nsummary: requests={s['requests']} new={s['ingested_new']} "
              f"duplicates={s['ingested_dup']} rejected={s['rejected']} "
              f"hidden_stream_queries={s['hidden_queries']}", flush=True)
        print(f"distinct candles held: {len(store.candles)}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
