"""PY-0005 - AURA MT5 bridge service.

A loopback-only JSON HTTP service that AURA's C++ runtime starts, supervises,
and shuts down. It uses only the Python standard library so it can boot,
handshake, and report MT5 unavailability even when MetaTrader5 is missing.

Endpoints (MT5_PYTHON_BRIDGE_V1.md):
    GET /v1/health
    GET /v1/handshake
    GET /v1/symbol
    GET /v1/candles?symbol=XAUUSD&timeframe=M15&count=500&closed_only=true
    GET /v1/tick?symbol=XAUUSD

Security boundary:
    * binds only to 127.0.0.1 (never 0.0.0.0);
    * validates protocol/schema versions and rejects mismatches;
    * rejects malformed requests and future-dated client timestamps;
    * does not log credentials.

Causality:
    * candle requests default to closed_only=true and the client skips the
      forming bar; the service never treats the current bar as a decision bar.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, Optional, Tuple
from urllib.parse import parse_qs, urlparse

from mt5_client import Mt5Client
from schemas import (
    ErrorInfo,
    PROTOCOL_VERSION,
    SCHEMA_VERSION,
    SOURCE_NAME,
    TIMEFRAMES,
    PRIMARY_OPERATIONAL_TIMEFRAME,
    PRIMARY_STRUCTURAL_TIMEFRAME,
    STATUS_ERROR,
    STATUS_OK,
    ERR_BAD_REQUEST,
    ERR_BRIDGE_PROTOCOL_MISMATCH,
    ERR_BRIDGE_SCHEMA_MISMATCH,
    ERR_MT5_SYMBOL_UNRESOLVED,
    ERR_MT5_TERMINAL_UNAVAILABLE,
    ERR_NOT_FOUND,
    ERR_INTERNAL,
    QUALITY_UNKNOWN,
    QUALITY_VALID,
    error_envelope,
    ok_envelope,
)

LOOPBACK_HOST = "127.0.0.1"
DEFAULT_PORT = 8791

# Tolerance for client clock skew when rejecting future-dated request stamps.
_FUTURE_SKEW_SECONDS = 300


class BridgeState:
    """Shared, long-lived service state."""

    def __init__(self, symbol: str = "XAUUSD") -> None:
        self.client = Mt5Client()
        self.preferred_symbol = symbol
        self.started_at = int(time.time())
        self.last_successful_request: Optional[int] = None
        self.last_error: str = ""
        self.resolved_symbol: str = ""
        self.mt5_ready = False

    def bootstrap(self) -> None:
        """Attempt MT5 init + symbol resolution. Failure is recorded, not fatal."""
        init = self.client.initialize()
        if not init.ok:
            self.mt5_ready = False
            self.last_error = f"{init.error_code}: {init.message}"
            return
        resolved = self.client.resolve_symbol(self.preferred_symbol)
        if not resolved.ok:
            self.mt5_ready = False
            self.last_error = f"{resolved.error_code}: {resolved.message}"
            return
        self.resolved_symbol = resolved.data.get("symbol", self.preferred_symbol)
        self.mt5_ready = True

    def shutdown(self) -> None:
        self.client.shutdown()

    def mark_success(self) -> None:
        self.last_successful_request = int(time.time())

    def mark_error(self, text: str) -> None:
        self.last_error = text


def _handshake_payload(state: BridgeState) -> Dict[str, Any]:
    return {
        "service": SOURCE_NAME,
        "protocol_version": PROTOCOL_VERSION,
        "schema_version": SCHEMA_VERSION,
        "timeframes": TIMEFRAMES,
        "primary_operational_timeframe": PRIMARY_OPERATIONAL_TIMEFRAME,
        "primary_structural_timeframe": PRIMARY_STRUCTURAL_TIMEFRAME,
        "loopback_only": True,
        "started_at_utc": state.started_at,
        "mt5_ready": state.mt5_ready,
        "resolved_symbol": state.resolved_symbol,
    }


def _health_payload(state: BridgeState) -> Dict[str, Any]:
    health = state.client.health()
    health.update({
        "process_state": "ONLINE",
        "mt5_ready": state.mt5_ready,
        "last_successful_request": state.last_successful_request,
        "last_error": state.last_error,
        "resolved_symbol": state.resolved_symbol,
        "generated_at_utc": int(time.time()),
    })
    return health


class BridgeHandler(BaseHTTPRequestHandler):
    server_version = "AURABridge/" + PROTOCOL_VERSION
    protocol_version = "HTTP/1.1"

    # Silence default stderr request logging noise.
    def log_message(self, fmt: str, *args: Any) -> None:  # noqa: A003
        pass

    # -- helpers ------------------------------------------------------------

    def _state(self) -> BridgeState:
        return self.server.state  # type: ignore[attr-defined]

    def _send_json(self, body: Dict[str, Any], http_status: int = 200) -> None:
        payload = json.dumps(body).encode("utf-8")
        self.send_response(http_status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _check_versions(self) -> Optional[Dict[str, Any]]:
        """Reject protocol/schema mismatches. Returns an error envelope if bad."""
        proto = self.headers.get("X-AURA-Protocol")
        schema = self.headers.get("X-AURA-Schema")
        if proto is not None:
            req_major = proto.split(".")[0].strip()
            our_major = PROTOCOL_VERSION.split(".")[0]
            if req_major != our_major:
                return error_envelope(
                    ErrorInfo(code=ERR_BRIDGE_PROTOCOL_MISMATCH,
                              message=f"client protocol {proto} incompatible with {PROTOCOL_VERSION}",
                              recovery="use a compatible protocol version"),
                    generated_at_utc=int(time.time()), source=SOURCE_NAME)
        if schema is not None:
            req_major = schema.split(".")[0].strip()
            our_major = SCHEMA_VERSION.split(".")[0]
            if req_major != our_major:
                return error_envelope(
                    ErrorInfo(code=ERR_BRIDGE_SCHEMA_MISMATCH,
                              message=f"client schema {schema} incompatible with {SCHEMA_VERSION}",
                              recovery="use a compatible schema version"),
                    generated_at_utc=int(time.time()), source=SOURCE_NAME)
        return None

    def _reject_future_stamp(self, query: Dict[str, list]) -> Optional[Dict[str, Any]]:
        raw = (query.get("client_time") or [None])[0]
        if raw is None:
            return None
        try:
            client_time = int(raw)
        except (TypeError, ValueError):
            return error_envelope(
                ErrorInfo(code=ERR_BAD_REQUEST, message="client_time is not an integer",
                          recovery="send client_time as unix seconds"),
                generated_at_utc=int(time.time()), source=SOURCE_NAME)
        if client_time > int(time.time()) + _FUTURE_SKEW_SECONDS:
            return error_envelope(
                ErrorInfo(code=ERR_BAD_REQUEST, message="client_time is future-dated",
                          recovery="synchronize the client clock"),
                generated_at_utc=int(time.time()), source=SOURCE_NAME)
        return None

    def _error(self, info: ErrorInfo, http_status: int = 200) -> None:
        self._send_json(error_envelope(info, generated_at_utc=int(time.time()),
                                       source=SOURCE_NAME), http_status)

    # -- routing ------------------------------------------------------------

    def do_GET(self) -> None:  # noqa: N802
        try:
            self._route()
        except Exception as exc:  # never leak a stack trace to the client
            self._error(ErrorInfo(code=ERR_INTERNAL, message=str(exc),
                                  recovery="retry or restart the bridge"), 500)

    def _route(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"
        query = parse_qs(parsed.query)
        state = self._state()

        version_error = self._check_versions()
        if version_error is not None:
            self._send_json(version_error, 400)
            return

        if path == "/v1/handshake":
            self._send_json(ok_envelope(_handshake_payload(state),
                                        generated_at_utc=int(time.time()),
                                        source=SOURCE_NAME))
            return

        if path == "/v1/health":
            self._send_json(ok_envelope(_health_payload(state),
                                        generated_at_utc=int(time.time()),
                                        source=SOURCE_NAME,
                                        quality=QUALITY_VALID if state.mt5_ready else QUALITY_UNKNOWN))
            return

        if path == "/v1/symbol":
            self._handle_symbol(state)
            return

        if path == "/v1/candles":
            future = self._reject_future_stamp(query)
            if future is not None:
                self._send_json(future, 400)
                return
            self._handle_candles(state, query)
            return

        if path == "/v1/tick":
            future = self._reject_future_stamp(query)
            if future is not None:
                self._send_json(future, 400)
                return
            self._handle_tick(state, query)
            return

        self._error(ErrorInfo(code=ERR_NOT_FOUND, message=f"unknown path {path}"), 404)

    def _handle_symbol(self, state: BridgeState) -> None:
        if not state.client.available:
            self._error(ErrorInfo(code=ERR_MT5_TERMINAL_UNAVAILABLE,
                                  message="MetaTrader5 package unavailable",
                                  recovery="install the bundled Python runtime"))
            return
        result = state.client.symbol_specification(state.resolved_symbol or state.preferred_symbol)
        if not result.ok:
            state.mark_error(result.message)
            self._error(ErrorInfo(code=result.error_code, message=result.message))
            return
        state.mark_success()
        self._send_json(ok_envelope(result.data, generated_at_utc=int(time.time()),
                                    source=SOURCE_NAME, symbol=state.resolved_symbol))

    def _handle_candles(self, state: BridgeState, query: Dict[str, list]) -> None:
        symbol = (query.get("symbol") or [state.resolved_symbol or state.preferred_symbol])[0]
        timeframe = (query.get("timeframe") or [PRIMARY_OPERATIONAL_TIMEFRAME])[0]
        count_raw = (query.get("count") or ["500"])[0]
        closed_raw = (query.get("closed_only") or ["true"])[0].lower()

        if timeframe not in TIMEFRAMES:
            self._error(ErrorInfo(code=ERR_BAD_REQUEST,
                                  message=f"unsupported timeframe {timeframe}",
                                  context={"supported": TIMEFRAMES},
                                  recovery="request a canonical timeframe"))
            return
        try:
            count = int(count_raw)
        except ValueError:
            self._error(ErrorInfo(code=ERR_BAD_REQUEST, message="count is not an integer"))
            return
        if count <= 0 or count > 100000:
            self._error(ErrorInfo(code=ERR_BAD_REQUEST,
                                  message="count out of range (1..100000)"))
            return
        closed_only = closed_raw in ("true", "1", "yes")

        result = state.client.read_candles(symbol, timeframe, count, closed_only)
        if not result.ok:
            state.mark_error(result.message)
            self._error(ErrorInfo(code=result.error_code, message=result.message,
                                  context={"symbol": symbol, "timeframe": timeframe},
                                  recovery="verify MT5 terminal, symbol, and data availability"))
            return
        state.mark_success()
        self._send_json(ok_envelope(result.data, generated_at_utc=int(time.time()),
                                    source=SOURCE_NAME, broker=state.client.broker,
                                    symbol=symbol, timeframe=timeframe,
                                    quality=QUALITY_VALID))

    def _handle_tick(self, state: BridgeState, query: Dict[str, list]) -> None:
        symbol = (query.get("symbol") or [state.resolved_symbol or state.preferred_symbol])[0]
        result = state.client.read_tick(symbol)
        if not result.ok:
            state.mark_error(result.message)
            self._error(ErrorInfo(code=result.error_code, message=result.message,
                                  context={"symbol": symbol},
                                  recovery="verify MT5 terminal connectivity"))
            return
        state.mark_success()
        self._send_json(ok_envelope(result.data, generated_at_utc=int(time.time()),
                                    source=SOURCE_NAME, broker=state.client.broker,
                                    symbol=symbol, quality=QUALITY_VALID))


class BridgeServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address: Tuple[str, int], state: BridgeState) -> None:
        super().__init__(address, BridgeHandler)
        self.state = state


def create_server(host: str = LOOPBACK_HOST, port: int = DEFAULT_PORT,
                  symbol: str = "XAUUSD") -> BridgeServer:
    if host != LOOPBACK_HOST:
        raise ValueError("bridge must bind to loopback only (127.0.0.1)")
    state = BridgeState(symbol)
    state.bootstrap()
    return BridgeServer((host, port), state)


def main(argv: Optional[list] = None) -> int:
    parser = argparse.ArgumentParser(description="AURA MT5 loopback bridge service")
    parser.add_argument("--host", default=LOOPBACK_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--symbol", default="XAUUSD")
    args = parser.parse_args(argv)

    try:
        server = create_server(args.host, args.port, args.symbol)
    except ValueError as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 2

    print(f"[OK] AURA bridge listening on http://{args.host}:{args.port} "
          f"(protocol {PROTOCOL_VERSION})", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.state.shutdown()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
