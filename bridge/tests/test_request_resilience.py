#!/usr/bin/env python3
"""PY-0008 - Regression teeth for the per-request bridge crash.

A field install reported that ``bridge_service.py`` printed, for every incoming
connection::

    Exception occurred during processing of request from ('127.0.0.1', ...)

with no traceback, so the crash was undiagnosable.

Root cause: the handler's write path could raise when a client hung up
mid-response (``BrokenPipeError`` / ``ConnectionResetError``). ``do_GET`` caught
the raise but then tried to write an error envelope to the same dead socket,
which raised again and escaped ``handle_one_request``; socketserver's default
``handle_error`` then printed the bare message with no cause. A connection reset
during request-line read did the same.

This test drives the *real* ``BridgeHandler`` over a real loopback socket,
including a client that disconnects mid-response, and asserts:

  1. a normal request still returns a valid envelope;
  2. the client-disconnect does not produce a traceback or the bare
     "Exception occurred" line on stderr;
  3. the server survives the disconnect and still serves the next request.

Run: python3 bridge/tests/test_request_resilience.py
     python3 -m pytest bridge/tests/ -v
"""

from __future__ import annotations

import io
import json
import os
import sys
import threading
import time
import types
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
BRIDGE_DIR = os.path.abspath(os.path.join(HERE, "..", "mt5_python"))
if BRIDGE_DIR not in sys.path:
    sys.path.insert(0, BRIDGE_DIR)

_results = []


def check(name: str, ok: bool, detail: str = "") -> None:
    _results.append((name, ok, detail))
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f" - {detail}" if detail else ""))


def _make_fake_mt5() -> types.ModuleType:
    mod = types.ModuleType("MetaTrader5")
    for k, v in dict(TIMEFRAME_M1=1, TIMEFRAME_M5=5, TIMEFRAME_M15=15, TIMEFRAME_M30=30,
                     TIMEFRAME_H1=16385, TIMEFRAME_H4=16388, TIMEFRAME_D1=16408,
                     TIMEFRAME_W1=32769, TIMEFRAME_MN1=49153).items():
        setattr(mod, k, v)

    class _DType:
        names = ("time", "open", "high", "low", "close",
                 "tick_volume", "spread", "real_volume")

    class _Rates:
        dtype = _DType()

        def __init__(self, rows):
            self._rows = rows

        def __len__(self):
            return len(self._rows)

        def __iter__(self):
            return iter(self._rows)

    class _Sym:
        def __init__(self, name):
            self.name = name

    class _Acc:
        company = "FakeBroker"
        server = "FakeServer"

    def initialize(*a, **k):
        return True

    def shutdown():
        return None

    def version():
        return (5000, 3800, "")

    def last_error():
        return (0, "ok")

    def account_info():
        return _Acc()

    def symbols_get():
        return [_Sym("XAUUSD")]

    def symbol_select(n, e=True):
        return True

    def symbol_info(s):
        return type("I", (), {"name": s, "digits": 2, "point": 0.01})()

    def symbol_info_tick(s):
        t = type("T", (), {})()
        t.time = int(time.time())
        t.bid = t.ask = t.last = 2000.0
        t.volume = 0.0
        t.flags = 6
        return t

    def copy_rates_from_pos(sym, tf, start, count):
        now = int(time.time())
        bars = [{"time": now - i * 900, "open": 2000.0, "high": 2001.0,
                 "low": 1999.0, "close": 2000.5, "tick_volume": 1, "spread": 20,
                 "real_volume": 0} for i in range(300)]
        bars.reverse()
        end = len(bars) - start
        return _Rates(bars[max(0, end - count):end]) if end > 0 else []

    for n, f in (("initialize", initialize), ("shutdown", shutdown),
                 ("version", version), ("last_error", last_error),
                 ("account_info", account_info), ("symbols_get", symbols_get),
                 ("symbol_select", symbol_select), ("symbol_info", symbol_info),
                 ("symbol_info_tick", symbol_info_tick),
                 ("copy_rates_from_pos", copy_rates_from_pos)):
        setattr(mod, n, f)
    return mod


class Server:
    def __init__(self) -> None:
        sys.modules["MetaTrader5"] = _make_fake_mt5()
        for stale in ("bridge_service", "mt5_client", "schemas"):
            sys.modules.pop(stale, None)
        import bridge_service  # noqa: E402

        self.bridge_service = bridge_service
        self.state = bridge_service.BridgeState("XAUUSD")
        self.state.bootstrap()
        self.server = bridge_service.BridgeServer(("127.0.0.1", 0), self.state)
        self.port = self.server.server_address[1]
        self._thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self._thread.start()

    def get(self, path: str):
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}")
        try:
            with urllib.request.urlopen(req, timeout=5) as r:
                return r.status, json.loads(r.read().decode())
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read().decode())

    def stop(self) -> None:
        self.server.shutdown()
        self.server.server_close()


def scenario_no_crash_on_disconnect() -> None:
    print("Scenario: per-request crash (client disconnect mid-response)")
    s = Server()
    # Capture handler-level noise. Before the fix, a disconnect wrote a bare
    # "Exception occurred during processing of request ..." (no traceback) here.
    captured = io.StringIO()
    real_stderr = sys.stderr
    sys.stderr = captured
    try:
        # Baseline: a normal request works.
        code, body = s.get("/v1/health")
        check("health 200", code == 200, f"http={code}")
        check("health envelope OK", body.get("status") == "OK")

        # A client that connects, sends a request, reads nothing and closes
        # immediately. The server must not surface an unhandled exception.
        import socket
        for path in ("/v1/health", "/v1/candles?tf=M15&limit=50",
                     "/v1/handshake", "/v1/symbol", "/v1/tick?symbol=XAUUSD"):
            sk = socket.create_connection(("127.0.0.1", s.port), timeout=3)
            sk.sendall(f"GET {path} HTTP/1.1\r\nHost: 127.0.0.1:{s.port}\r\n"
                       f"Connection: close\r\n\r\n".encode())
            sk.close()  # hang up before the response is read

        # A connection that sends nothing and resets.
        for _ in range(5):
            sk = socket.create_connection(("127.0.0.1", s.port), timeout=3)
            sk.close()

        time.sleep(0.5)

        # The server must have survived and still answer normally.
        code, body = s.get("/v1/candles?tf=M15&limit=10")
        check("server survives disconnect", code == 200, f"http={code}")
        check("response still valid",
              body.get("status") == "OK"
              and body.get("payload", {}).get("count") == 10)
    finally:
        sys.stderr = real_stderr
        s.stop()

    noise = captured.getvalue()
    check("no unhandled traceback on stderr",
          "Traceback (most recent call last)" not in noise, repr(noise[:200]))
    check("no bare socketserver 'Exception occurred' line",
          "Exception occurred during processing" not in noise, repr(noise[:200]))


def main() -> int:
    print(f"Repo bridge dir: {BRIDGE_DIR}")
    scenario_no_crash_on_disconnect()
    passed = sum(1 for _, ok, _ in _results if ok)
    total = len(_results)
    print(f"{passed}/{total} checks passed")
    return 0 if passed == total else 1


def test_request_resilience_checks() -> None:
    assert main() == 0, "request resilience checks failed"


def test_pytest_actually_collects_this_suite() -> None:
    discovered = [n for n in globals() if n.startswith("test_")]
    assert "test_request_resilience_checks" in discovered
    assert len(discovered) >= 2


if __name__ == "__main__":
    raise SystemExit(main())
