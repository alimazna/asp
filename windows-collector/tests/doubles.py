"""Test doubles for the two external boundaries.

Both are TEST DATA ONLY. Nothing here runs in production and nothing here is
ever written to the real candle store. They exist so the collector's real logic
(identity, storage, planning, delivery, acknowledgment) can be exercised on a
machine with no MetaTrader terminal and no live receiver.

The MT5 double mirrors the real package's contract exactly where it matters:

  * ``copy_rates_from_pos(symbol, timeframe, start_pos, count)`` returns bars
    NEWEST-FIRST, with index 0 being the forming bar. ``start_pos=1`` therefore
    yields closed bars only. The double reproduces this so the off-by-one that
    submits a forming candle is caught by the tests.
"""

from __future__ import annotations

import json
import urllib.parse
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, List, Optional

# ---------------------------------------------------------------- MT5 double

MT5_TIMEFRAME_VALUES = {
    "M1": 1, "M5": 5, "M15": 15, "M30": 30,
    "H1": 16385, "H4": 16388, "D1": 16408, "W1": 32769, "MN1": 49153,
}
TF_SECONDS = {
    "M1": 60, "M5": 300, "M15": 900, "M30": 1800, "H1": 3600,
    "H4": 14400, "D1": 86400, "W1": 604800, "MN1": 2592000,
}


class FakeRate(dict):
    """A dict that also supports attribute access, like MT5's namedtuple."""

    def __getattr__(self, name):
        try:
            return self[name]
        except KeyError:
            raise AttributeError(name) from None


class FakeMt5Terminal:
    """Deterministic synthetic feed. Test data, never a production source."""

    def __init__(self, symbol: str = "XAUUSD", total_bars: int = 300,
                 last_time: Optional[int] = None, broker: str = "FakeBroker-Demo",
                 clock_offset_seconds: int = 0, price: float = 2000.0):
        self.symbol = symbol
        self.total_bars = total_bars
        self.clock_offset_seconds = clock_offset_seconds
        self.broker = broker
        self.available = True
        #: Newest *forming* bar open time, in broker time.
        self.last_time = int(last_time if last_time is not None
                             else time.time())
        self._base_price = price

    def set_available(self, available: bool) -> None:
        self.available = available

    def _bar(self, timeframe: str, start_pos: int) -> FakeRate:
        """One bar at ``start_pos`` where 0 is the forming bar.

        A real broker aligns each timeframe's bars to that timeframe's own grid,
        so the forming bar is the current period. Anchoring here reproduces
        that: without it the synthetic bars land off-grid and drift by seconds
        between runs, which no real terminal does.
        """
        step = TF_SECONDS[timeframe]
        anchor = self.last_time - (self.last_time % step)
        open_time = anchor - start_pos * step
        # Deterministic pseudo-price: reproducible across runs, so a test can
        # assert on exact values without a fixture file.
        seed = abs(hash((self.symbol, timeframe, open_time))) % 1000
        drift = (seed - 500) / 100.0
        open_ = self._base_price + drift
        close = open_ + (seed % 7 - 3) / 10.0
        high = max(open_, close) + (seed % 5) / 10.0
        low = min(open_, close) - (seed % 5) / 10.0
        return FakeRate({
            "time": open_time, "open": round(open_, 2),
            "high": round(high, 2), "low": round(low, 2),
            "close": round(close, 2), "tick_volume": 100 + seed,
            "spread": 20, "real_volume": 0,
        })

    # -- MetaTrader5-shaped API ---------------------------------------------

    def symbol_info(self, symbol: str):
        if not self.available or symbol != self.symbol:
            return None
        return FakeRate({"name": symbol, "server": self.broker,
                         "visible": True})

    def symbol_info_tick(self, symbol: str):
        if not self.available or symbol != self.symbol:
            return None
        # The tick clock is broker time, which is what the offset measures.
        return FakeRate({"time": int(time.time()) + self.clock_offset_seconds,
                         "bid": self._base_price, "ask": self._base_price + 0.2})

    def copy_rates_from_pos(self, symbol: str, timeframe: int,
                            start_pos: int, count: int):
        if not self.available or symbol != self.symbol:
            return None
        name = _tf_name(timeframe)
        if name is None:
            return None
        bars: List[FakeRate] = []
        for offset in range(start_pos, start_pos + count):
            if offset >= self.total_bars:
                break
            bars.append(self._bar(name, offset))
        return bars

    def last_error(self):
        return (0, "ok") if self.available else (-10001, "terminal unavailable")


def _tf_name(value: int) -> Optional[str]:
    for name, code in MT5_TIMEFRAME_VALUES.items():
        if code == value:
            return name
    return None


# ----------------------------------------------------------- receiver double

class FakeReceiverState:
    """Shared state for the fake receiver. Test data only."""

    def __init__(self):
        self.lock = threading.Lock()
        #: uid -> candle payload the receiver has "persisted"
        self.stored: Dict[str, Dict[str, Any]] = {}
        self.requests: List[Dict[str, Any]] = []
        self.ingest_calls = 0
        self.token = "test-token"
        self.fail_next = 0          # number of upcoming ingests to drop (no reply)
        self.commit_then_fail = False  # commit, then lose the response
        self.delay_seconds = 0.0
        self.health_ok = True
        #: (symbol, timeframe) pairs to report as holding nothing. Models a
        #: receiver that lost a stream (restored database, partial restore)
        #: while still answering for the others.
        self.empty_streams: set = set()
        #: How an empty stream is signalled. The real ASTRA server answers 503
        #: `dependency_unavailable`; some builds answer 200 with no bars. Both
        #: must be understood as an authoritative absence.
        self.empty_mode = "503"


class FakeReceiverHandler(BaseHTTPRequestHandler):
    state: FakeReceiverState = None  # type: ignore

    def log_message(self, *args):  # silence the default stderr noise
        pass

    def _authorized(self) -> bool:
        header = self.headers.get("Authorization", "")
        return header == "Bearer " + self.state.token

    def _reply(self, status: int, payload: Dict[str, Any]) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):  # noqa: N802
        path = self.path.split("?")[0]
        # Serve under the same version prefix as the real ASTRA server. The
        # double used to answer the bare paths, which meant a client pointed at
        # a host without "/api/v1" passed here and 404'd against the real
        # server. Keeping the prefix makes the double able to catch that class
        # of misconfiguration instead of hiding it.
        for prefix in ("/api/v1",):
            if path.startswith(prefix + "/"):
                path = path[len(prefix):]
                break
        if path == "/health":
            # Unauthenticated, matching the real ASTRA server: health is a
            # liveness probe and the C++ server does not gate it. The double
            # previously required a token here, which is a contract divergence —
            # a client that sent no token would have passed against the double
            # and failed against the real receiver.
            if not self.state.health_ok:
                self._reply(503, {"error": "true", "code": "unavailable"})
                return
            self._reply(200, {"status": "ok"})
            return
        if path == "/sync/latest":
            # Authenticated read route, used to verify a credential without
            # writing anything. Mirrors the real server, which requires the
            # sender bearer token here.
            if not self._authorized():
                self._reply(401, {"error": "true", "code": "unauthorized"})
                return
            with self.state.lock:
                candles = sorted(self.state.stored.values(),
                                 key=lambda c: c.get("time", 0))
            latest = candles[-1] if candles else None
            self._reply(200, {"status": "ok", "data": {"latest": latest}})
            return
        if path == "/candles":
            if not self._authorized():
                self._reply(401, {"error": "true", "code": "unauthorized"})
                return
            query = urllib.parse.parse_qs(self.path.split("?", 1)[1]
                                          if "?" in self.path else "")
            symbol = (query.get("symbol") or [""])[0]
            timeframe = (query.get("timeframe") or [""])[0]
            if (symbol, timeframe) in self.state.empty_streams:
                # A stream the receiver lost. Answered the way the real server
                # answers a stream it holds nothing for.
                if self.state.empty_mode == "503":
                    self._reply(503, {"error": "true",
                                      "code": "dependency_unavailable",
                                      "message": ("no candle data available for "
                                                  + timeframe)})
                else:
                    self._reply(200, {"data": {
                        "bars": [], "symbol": symbol, "timeframe": timeframe,
                        "count": 0, "closed_only": True,
                        "newest_closed_time": 0}})
                return
            with self.state.lock:
                candles = [dict(c) for c in self.state.stored.values()]
            # A real receiver scopes a stream query to the requested symbol and
            # timeframe. The double must too, or it would hide a client that
            # fails to scope its own comparison.
            if symbol:
                candles = [c for c in candles if c.get("symbol") == symbol]
            if timeframe:
                candles = [c for c in candles
                           if c.get("timeframe") == timeframe]
            # Mirror the real ASTRA server exactly: `data.bars[]` with the open
            # time under `time`. The double previously answered with
            # `candles`/`open_times`, which is the collector's own dialect — a
            # receiver the real server has never emitted. Tests written against
            # that passed while reconciliation silently found nothing in
            # production, so the dialect is now the server's.
            bars = [{
                "time": int(c["open_time_utc"]),
                "open": c["open"], "high": c["high"],
                "low": c["low"], "close": c["close"],
                "tick_volume": c.get("tick_volume", 0),
                "spread": c.get("spread", 0),
                "real_volume": c.get("real_volume", 0),
            } for c in candles]
            self._reply(200, {"data": {
                "bars": bars,
                "timeframe": timeframe,
                "symbol": symbol,
                "count": len(bars),
                "closed_only": True,
                "newest_closed_time": bars[-1]["time"] if bars else 0}})
            return
        self._reply(404, {"error": "true", "code": "not_found"})

    def do_POST(self):  # noqa: N802
        path = self.path.split("?")[0]
        for prefix in ("/api/v1",):
            if path.startswith(prefix + "/"):
                path = path[len(prefix):]
                break
        if path != "/candles/ingest":
            self._reply(404, {"error": "true", "code": "not_found"})
            return
        if not self._authorized():
            self._reply(401, {"error": "true", "code": "unauthorized"})
            return
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b"{}"
        try:
            body = json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError:
            self._reply(400, {"error": "true", "code": "bad_json"})
            return
        self.state.ingest_calls += 1
        with self.state.lock:
            self.state.requests.append(body)

        # The real server validates the batch envelope before it reads a single
        # candle and answers HTTP 400 when source_id/symbol/timeframe is absent.
        # The double used to skip this check, which is how the collector shipped
        # without sending source_id at all while every test passed.
        for field in ("source_id", "symbol", "timeframe"):
            if not body.get(field):
                self._reply(400, {"error": "true", "code": "missing_fields",
                                  "message": ("source_id, symbol and timeframe "
                                              "are required")})
                return

        if self.state.delay_seconds:
            time.sleep(self.state.delay_seconds)

        # The real server evaluates its future guard in the sender's broker
        # frame. Reproduce that here, or a collector that forgets to send the
        # offset still looks correct against this double.
        offset = int(body.get("clock_offset_seconds", 0) or 0)
        broker_now = int(time.time()) + offset

        accepted, duplicates, rejected = [], [], []
        with self.state.lock:
            for candle in body.get("candles", []):
                uid = candle.get("uid") or ""
                if not uid:
                    rejected.append({"uid": "", "reason": "missing uid"})
                    continue
                # The real server reads the open time from `time`. Accepting
                # `open_time_utc` here hid a payload the receiver rejects
                # wholesale with HTTP 200 and `ingested: 0`.
                if "time" not in candle:
                    rejected.append({"uid": uid, "state": "rejected",
                                     "reason": ("candle rejected: candle 'time' "
                                                "missing or not numeric")})
                    continue
                try:
                    open_time = int(candle["time"])
                except (TypeError, ValueError):
                    rejected.append({"uid": uid, "state": "rejected",
                                     "reason": ("candle rejected: candle 'time' "
                                                "missing or not numeric")})
                    continue
                if open_time <= 0:
                    rejected.append({"uid": uid, "state": "rejected",
                                     "reason": ("candle rejected: candle "
                                                "open_time must be a positive "
                                                "epoch timestamp")})
                    continue
                if open_time > broker_now + 300:
                    rejected.append({"uid": uid, "state": "rejected",
                                     "reason": ("candle rejected: candle "
                                                "open_time is in the future")})
                    continue
                # The real server names a candle by its own identity string,
                # never by the sender's uid. The double mirrors that, so a
                # client that only understands echoed uids fails here instead
                # of in production.
                identity = ("symbol=%s&timeframe=%s&open_time=%d"
                            % (candle.get("symbol", ""),
                               candle.get("timeframe", ""), open_time))
                if uid in self.state.stored:
                    duplicates.append({"identity": identity,
                                       "state": "already_stored"})
                    continue
                # Store the row the way the real server does: the open time
                # lives in its own column (`open_time_utc`), and only the GET
                # handler renders it as `time`. Keeping the wire key here would
                # make the double's storage a shape the real server never has.
                row = dict(candle)
                row["open_time_utc"] = open_time
                self.state.stored[uid] = row
                accepted.append({"identity": identity, "state": "stored_new"})

        if self.state.commit_then_fail:
            # Data is committed but the response is lost: the classic
            # at-least-once case the sender must survive.
            self.state.commit_then_fail = False
            self.close_connection = True
            return
        if self.state.fail_next > 0:
            self.state.fail_next -= 1
            self.close_connection = True
            return

        self._reply(200, {"data": {
            "accepted": accepted, "duplicates": duplicates,
            "rejected": rejected, "received": int(time.time()),
            "stored_total": len(self.state.stored)}})


class FakeReader:
    """Duck-typed stand-in for ``Mt5Reader``.

    Implements the same public surface the engine uses, so the engine's real
    logic runs unchanged against a deterministic feed. It also mirrors the real
    reader's closed-candle discipline (start at index 1, then drop any bar that
    has not finished forming).
    """

    def __init__(self, terminal: FakeMt5Terminal, symbol: str = "XAUUSD"):
        self.terminal = terminal
        self._requested = symbol
        self._resolved = ""
        self._offset: Optional[int] = None
        self.connect_calls = 0

    def connect(self) -> None:
        self.connect_calls += 1
        if not self.terminal.available:
            from collector.mt5_reader import MT5Unavailable
            raise MT5Unavailable("MT5_TERMINAL_UNAVAILABLE", "fake terminal down")
        if self.terminal.symbol_info(self.terminal.symbol) is None:
            from collector.mt5_reader import MT5Unavailable
            raise MT5Unavailable("MT5_SYMBOL_UNAVAILABLE", "symbol missing")
        self._resolved = self.terminal.symbol

    def disconnect(self) -> None:
        self._resolved = ""

    @property
    def resolved_symbol(self) -> str:
        return self._resolved

    @property
    def broker(self) -> str:
        return self.terminal.broker

    def clock_offset_seconds(self) -> int:
        if self._offset is None:
            self._offset = self.terminal.clock_offset_seconds
        return self._offset

    def read_closed(self, timeframe: str, count: int, skip: int = 0):
        from collector.mt5_reader import MT5Unavailable
        if not self._resolved:
            raise MT5Unavailable("MT5_NOT_CONNECTED", "not connected")
        if not self.terminal.available:
            raise MT5Unavailable("MT5_TERMINAL_UNAVAILABLE", "fake terminal down")
        raw = self.terminal.copy_rates_from_pos(
            self._resolved, MT5_TIMEFRAME_VALUES[timeframe], 1 + int(skip),
            int(count))
        if raw is None:
            raise MT5Unavailable("MT5_READ_FAILED", "fake read failed")
        from collector.mt5_reader import RawCandle
        candles = [
            RawCandle(symbol=self._resolved, timeframe=timeframe,
                      open_time_utc=int(r["time"]), open=float(r["open"]),
                      high=float(r["high"]), low=float(r["low"]),
                      close=float(r["close"]), tick_volume=int(r["tick_volume"]),
                      spread=int(r["spread"]), real_volume=int(r["real_volume"]),
                      broker=self.terminal.broker)
            for r in raw
        ]
        candles.sort(key=lambda c: c.open_time_utc)
        return candles


class FakeReceiver:
    """Runs the fake receiver on an ephemeral port."""

    def __init__(self, token: str = "test-token"):
        self.state = FakeReceiverState()
        self.state.token = token
        handler = type("BoundHandler", (FakeReceiverHandler,),
                       {"state": self.state})
        self._httpd = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self._thread = threading.Thread(target=self._httpd.serve_forever,
                                        daemon=True)
        self._thread.start()

    @property
    def url(self) -> str:
        host, port = self._httpd.server_address[:2]
        return f"http://127.0.0.1:{port}"

    def stop(self) -> None:
        self._httpd.shutdown()

    def __enter__(self) -> "FakeReceiver":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.stop()
