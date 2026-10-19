"""MT5 closed-candle reader.

Reads only fully closed candles. MetaTrader's ``copy_rates_from_pos(symbol,
timeframe, start_pos, count)`` indexes bars newest-first where index 0 is the
*forming* bar, so a read must start at ``start_pos=1`` to get closed bars only.
Assuming index 0 is closed is the classic off-by-one that submits a candle whose
close is still moving; this module never does that.

The MT5 package is imported lazily so the rest of the collector (store, ledger,
planner, receiver) can be imported, tested, and reasoned about on a machine
without MetaTrader installed.
"""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Sequence

from .identity import TIMEFRAME_SECONDS, is_known_timeframe

# MetaTrader5 timeframe constants (values are stable across releases).
MT5_TIMEFRAMES = {
    "M1": 1,
    "M5": 5,
    "M15": 15,
    "M30": 30,
    "H1": 16385,
    "H4": 16388,
    "D1": 16408,
    "W1": 32769,
    "MN1": 49153,
}

# Broker symbol names vary; try these in order and use whatever resolves.
DEFAULT_SYMBOL_CANDIDATES = ["XAUUSD", "GOLD", "XAUUSDm", "XAUUSD.a", "XAUUSD.raw"]


@dataclass
class RawCandle:
    """One closed candle exactly as the source reported it."""

    symbol: str
    timeframe: str
    open_time_utc: int
    open: float
    high: float
    low: float
    close: float
    tick_volume: int = 0
    spread: int = 0
    real_volume: int = 0
    broker: str = ""


class MT5Unavailable(Exception):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


class Mt5Reader:
    """Thin, honest wrapper over the MetaTrader5 package."""

    def __init__(self, symbol: str = "XAUUSD",
                 symbol_candidates: Optional[Sequence[str]] = None,
                 terminal_path: str = "", timeout_ms: int = 15000):
        self._requested_symbol = symbol
        self._candidates = list(symbol_candidates or DEFAULT_SYMBOL_CANDIDATES)
        if symbol and symbol not in self._candidates:
            self._candidates.insert(0, symbol)
        self._terminal_path = terminal_path
        self._timeout_ms = timeout_ms
        self._mt5: Any = None
        self._resolved_symbol: str = ""
        self._broker: str = ""
        self._clock_offset: Optional[int] = None

    # -- lifecycle ----------------------------------------------------------

    def connect(self) -> None:
        """Attach to the running terminal. Never asks for a broker password."""
        try:
            import MetaTrader5 as mt5  # type: ignore
        except ImportError as exc:
            raise MT5Unavailable(
                "MT5_PACKAGE_MISSING",
                f"MetaTrader5 package is not installed: {exc}") from None
        self._mt5 = mt5
        kwargs: Dict[str, Any] = {"timeout": self._timeout_ms}
        if self._terminal_path:
            kwargs["path"] = self._terminal_path
        try:
            ok = mt5.initialize(**kwargs)
        except Exception as exc:
            raise MT5Unavailable("MT5_INIT_FAILED", str(exc)) from None
        if not ok:
            error = mt5.last_error()
            raise MT5Unavailable(
                "MT5_TERMINAL_UNAVAILABLE",
                f"MetaTrader5 initialize failed: {error}")
        self._resolved_symbol = self._discover_symbol()
        if not self._resolved_symbol:
            raise MT5Unavailable(
                "MT5_SYMBOL_UNAVAILABLE",
                "none of the candidate symbols exist on this broker: "
                + ", ".join(self._candidates))
        info = mt5.symbol_info(self._resolved_symbol)
        self._broker = getattr(info, "server", "") or "" if info else ""

    def disconnect(self) -> None:
        if self._mt5 is not None:
            try:
                self._mt5.shutdown()
            except Exception:
                pass
        self._mt5 = None

    def _discover_symbol(self) -> str:
        """Pick the symbol this broker actually uses. Never guesses silently:
        the resolved name is reported so the user can see which instrument is
        in use."""
        for candidate in self._candidates:
            try:
                if self._mt5.symbol_info(candidate) is not None:
                    return candidate
            except Exception:
                continue
        return ""

    # -- properties ---------------------------------------------------------

    @property
    def resolved_symbol(self) -> str:
        return self._resolved_symbol

    @property
    def broker(self) -> str:
        return self._broker

    def clock_offset_seconds(self) -> int:
        """Measured (broker server time - UTC), cached.

        MetaTrader stamps bar open times in broker server time. Measuring the
        offset once lets the future-dating guard run in the broker's frame
        instead of comparing broker stamps against a UTC clock, which made every
        intraday bar from a UTC+3 broker look ~3h future-dated.
        """
        if self._clock_offset is not None:
            return self._clock_offset
        offset = 0
        try:
            tick = self._mt5.symbol_info_tick(self._resolved_symbol)
            if tick is not None and getattr(tick, "time", 0):
                offset = int(tick.time) - int(time.time())
        except Exception:
            offset = 0
        # A real timezone is within UTC-12..UTC+14. Anything larger is a stale
        # feed, not a timezone; treat it as unknown rather than shifting data.
        if offset < -12 * 3600 or offset > 14 * 3600:
            offset = 0
        self._clock_offset = offset
        return offset

    # -- reads --------------------------------------------------------------

    def read_closed(self, timeframe: str, count: int) -> List[RawCandle]:
        """Newest ``count`` *closed* candles, oldest-first.

        ``start_pos=1`` skips the forming bar at index 0.
        """
        if self._mt5 is None:
            raise MT5Unavailable("MT5_NOT_CONNECTED", "terminal not connected")
        if not is_known_timeframe(timeframe):
            raise MT5Unavailable("MT5_BAD_TIMEFRAME",
                                 f"unknown timeframe: {timeframe!r}")
        if count <= 0:
            return []
        mt5_timeframe = MT5_TIMEFRAMES[timeframe]
        try:
            rates = self._mt5.copy_rates_from_pos(
                self._resolved_symbol, mt5_timeframe, 1, int(count))
        except Exception as exc:
            raise MT5Unavailable("MT5_READ_FAILED", str(exc)) from None
        if rates is None:
            error = self._mt5.last_error()
            raise MT5Unavailable("MT5_READ_FAILED",
                                 f"copy_rates_from_pos returned None: {error}")
        candles: List[RawCandle] = []
        for rate in rates:
            candles.append(RawCandle(
                symbol=self._resolved_symbol,
                timeframe=timeframe,
                open_time_utc=int(rate["time"]),
                open=float(rate["open"]),
                high=float(rate["high"]),
                low=float(rate["low"]),
                close=float(rate["close"]),
                tick_volume=int(rate["tick_volume"]),
                spread=int(rate["spread"]),
                real_volume=int(rate["real_volume"]),
                broker=self._broker,
            ))
        candles.sort(key=lambda c: c.open_time_utc)
        return candles

    def read_latest_closed(self, timeframe: str) -> Optional[RawCandle]:
        candles = self.read_closed(timeframe, 1)
        return candles[-1] if candles else None


def drop_forming_candle(candles: Sequence[RawCandle], timeframe: str,
                        now_utc: int, clock_offset_seconds: int = 0
                        ) -> List[RawCandle]:
    """Remove any candle whose bar has not fully closed yet.

    Defence in depth: even though the read starts at index 1, a bar is only
    genuinely closed once ``open_time + duration <= broker_now``. The last
    bar returned can still be forming at the boundary, so it is filtered here.
    """
    if timeframe not in TIMEFRAME_SECONDS:
        return list(candles)
    broker_now = now_utc + clock_offset_seconds
    step = TIMEFRAME_SECONDS[timeframe]
    closed = []
    for candle in candles:
        if candle.open_time_utc + step <= broker_now:
            closed.append(candle)
    return closed
