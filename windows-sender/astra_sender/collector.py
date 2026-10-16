"""Closed-candle collector for the Windows sender.

Wraps the real AURA MT5 reader (``bridge/mt5_python/mt5_client.py``), which
already enforces the closed-bar-only contract via MT5's ``start_pos=1``
semantics, symbol resolution, and freshness/staleness reporting. The collector
adds the sender-facing concerns:

  * one read per canonical timeframe (never the currently forming bar);
  * validation of OHLC relationships and UTC timestamps;
  * honest "unavailable" results (no fabricated bars when the package or
    terminal is missing);
  * explicit support for the fake MT5 module under test on non-Windows hosts.

The adapter imports the real ``mt5_client`` module. When MetaTrader5 is absent
the package reports availability honestly (``MT5_TERMINAL_UNAVAILABLE``).
"""

from __future__ import annotations

import importlib
import os
import sys
import time
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional


# The real bridge module lives at <repo>/bridge/mt5_python. Under PyInstaller
# frozen apps the module is copied beside the app; under source tree we reach it
# through PYTHONPATH or a sibling directory.
_MT5_CLIENT_NAMES = ("mt5_client", "astra_sender.mt5_client")
_BRIDGE_HINTS = [
    os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "bridge",
                                  "mt5_python")),
    os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..",
                                  "bridge", "mt5_python")),
]


def _load_mt5_client():
    """Load and return the mt5_client module (real bridge code, stdlib-only)."""
    for name in _MT5_CLIENT_NAMES:
        try:
            return importlib.import_module(name)
        except ImportError:
            continue
    for hint in _BRIDGE_HINTS:
        if hint not in sys.path:
            sys.path.insert(0, hint)
        try:
            return importlib.import_module("mt5_client")
        except ImportError:
            continue
    raise ImportError("cannot locate bridge/mt5_python/mt5_client.py")


@dataclass
class ClosedCandle:
    """A validated closed candle as surfaced by the sender (minimal schema)."""

    symbol: str
    timeframe: str
    time: int            # UTC epoch seconds of open
    open: float
    high: float
    low: float
    close: float
    tick_volume: int
    spread: int
    real_volume: int

    def to_payload(self) -> Dict[str, Any]:
        return {
            "symbol": self.symbol,
            "timeframe": self.timeframe,
            "time": int(self.time),
            "open": float(self.open),
            "high": float(self.high),
            "low": float(self.low),
            "close": float(self.close),
            "tick_volume": int(self.tick_volume),
            "spread": int(self.spread),
            "real_volume": int(self.real_volume),
        }


@dataclass
class CollectorStartup:
    ok: bool
    error_code: str = ""
    message: str = ""
    resolved_symbol: str = ""
    broker: str = ""
    server: str = ""
    terminal_version: Optional[tuple] = None


class ClosedCandleCollector:
    def __init__(self, preferred_symbol: str = "XAUUSD",
                 mt5_path: str = "", mt5_timeout_ms: int = 15000):
        self._client = None
        self._preferred = preferred_symbol
        self._resolved = ""
        self._broker = ""
        self._server = ""
        self._mt5_path = mt5_path
        self._mt5_timeout_ms = mt5_timeout_ms

    @property
    def resolved_symbol(self) -> str:
        return self._resolved

    @property
    def broker(self) -> str:
        return self._broker

    @property
    def server(self) -> str:
        return self._server

    # -- lifecycle ----------------------------------------------------------

    def start(self) -> CollectorStartup:
        try:
            module = _load_mt5_client()
        except ImportError as exc:
            return CollectorStartup(ok=False, error_code="MT5_CLIENT_MISSING",
                                    message=str(exc))
        self._client = module.Mt5Client()

        result = self._client.initialize(path=self._mt5_path,
                                         timeout_ms=self._mt5_timeout_ms)
        if not result.ok:
            return CollectorStartup(ok=False, error_code=result.error_code,
                                    message=result.message)

        symbol_result = self._client.resolve_symbol(self._preferred)
        if not symbol_result.ok:
            return CollectorStartup(ok=False, error_code=symbol_result.error_code,
                                    message=symbol_result.message)
        self._resolved = self._client.resolved_symbol
        self._broker = self._client.broker
        self._server = str(getattr(self._client, "server", "") or
                           getattr(self._client, "_server", ""))
        return CollectorStartup(ok=True, resolved_symbol=self._resolved,
                                broker=self._broker, server=self._server)

    def stop(self) -> None:
        if self._client is not None:
            try:
                self._client.shutdown()
            except Exception:
                pass

    # -- reads --------------------------------------------------------------

    def read_latest_closed(self, timeframe: str) -> Dict[str, Any]:
        """Read the newest closed candle for a timeframe.

        Never returns the currently forming candle (the bridge client starts at
        ``start_pos=1``). Returns a dict with either:
          * ``ok: True`` and ``candle`` a :class:`ClosedCandle`;
          * ``ok: False`` with ``error_code``/``message`` (honest absence).
        """
        if self._client is None:
            return {"ok": False, "error_code": "MT5_CLIENT_NOT_STARTED",
                    "message": "collector not started"}
        result = self._client.read_candles(self._resolved or self._preferred,
                                           timeframe, 3, closed_only=True)
        if not result.ok:
            return {"ok": False, "error_code": result.error_code,
                    "message": result.message}
        candles = result.data.get("candles") or []
        if not candles:
            return {"ok": False, "error_code": "MARKET_DATA_MISSING",
                    "message": "no closed candles available"}
        newest = candles[-1]
        candle = ClosedCandle(
            symbol=self._resolved or result.data.get("symbol", self._preferred),
            timeframe=timeframe,
            time=int(newest["time"]),
            open=float(newest["open"]),
            high=float(newest["high"]),
            low=float(newest["low"]),
            close=float(newest["close"]),
            tick_volume=int(newest.get("tick_volume", 0)),
            spread=int(newest.get("spread", 0)),
            real_volume=int(newest.get("real_volume", 0)),
        )
        validation_error = self._validate(candle)
        if validation_error:
            return {"ok": False, "error_code": "MARKET_DATA_INVALID",
                    "message": validation_error}
        return {"ok": True, "candle": candle, "freshness": result.data.get("freshness", "UNKNOWN")}

    def read_recent_closed(self, timeframe: str, count: int) -> Dict[str, Any]:
        """Read up to `count` newest closed candles (for bounded catch-up)."""
        if self._client is None:
            return {"ok": False, "error_code": "MT5_CLIENT_NOT_STARTED",
                    "message": "collector not started"}
        result = self._client.read_candles(self._resolved or self._preferred,
                                           timeframe, count, closed_only=True)
        if not result.ok:
            return {"ok": False, "error_code": result.error_code,
                    "message": result.message}
        candles = result.data.get("candles") or []
        validated: List[ClosedCandle] = []
        for raw in candles:
            candle = ClosedCandle(
                symbol=self._resolved or result.data.get("symbol", self._preferred),
                timeframe=timeframe,
                time=int(raw["time"]),
                open=float(raw["open"]),
                high=float(raw["high"]),
                low=float(raw["low"]),
                close=float(raw["close"]),
                tick_volume=int(raw.get("tick_volume", 0)),
                spread=int(raw.get("spread", 0)),
                real_volume=int(raw.get("real_volume", 0)),
            )
            if self._validate(candle) is None:
                validated.append(candle)
        return {"ok": True, "candles": validated}

    @staticmethod
    def _validate(candle: ClosedCandle) -> Optional[str]:
        """Validate OHLC relationships and timestamps (mirrors the server)."""
        if not (candle.open > 0 and candle.high > 0 and candle.low > 0
                and candle.close > 0):
            return "non-positive OHLC values"
        body_high = max(candle.open, candle.close)
        body_low = min(candle.open, candle.close)
        if candle.high < candle.low or candle.high < body_high or candle.low > body_low:
            return "OHLC ordering violated"
        if candle.tick_volume < 0 or candle.real_volume < 0 or candle.spread < 0:
            return "negative volume/spread"
        if candle.time <= 0:
            return "non-positive candle open time"
        now = int(time.time())
        if candle.time > now + 3600:
            return "candle open time is far in the future"
        return None