"""PY-0003 - MT5 client wrapper.

Owns all contact with the MetaTrader5 package. If the package is missing (for
example on a non-Windows machine) the client still constructs and reports
`available = False` with an explicit reason; it never fabricates market data.

Causality: candle retrieval always skips the currently forming bar using
`start_pos=1` semantics, so decision-grade data is closed-bar only.
"""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

from schemas import TIMEFRAMES

# Nominal bar duration in seconds per canonical timeframe. Used only to judge
# whether the newest closed bar is stale; MN1 is a 30-day approximation.
_TIMEFRAME_SECONDS = {
    "M1": 60,
    "M5": 5 * 60,
    "M15": 15 * 60,
    "M30": 30 * 60,
    "H1": 60 * 60,
    "H4": 4 * 60 * 60,
    "D1": 24 * 60 * 60,
    "W1": 7 * 24 * 60 * 60,
    "MN1": 30 * 24 * 60 * 60,
}

# A stream is STALE when the newest closed bar is older than this multiple of
# the timeframe's nominal duration. The multiplier tolerates weekends and short
# market closures without masking a genuinely dead feed.
_STALE_INTERVAL_MULTIPLIER = 3

# Largest |server - local UTC| we accept as a genuine broker timezone offset
# (real brokers sit within a few hours of UTC; the extreme timezone is UTC+14).
# A larger implied offset means the newest bar is old (a weekend or a stale
# feed), not that the server clock is exotic, so it is not treated as skew.
_MAX_CLOCK_OFFSET_SECONDS = 18 * 3600

try:  # The package is Windows-only; absence must be reportable, not fatal.
    import MetaTrader5 as _mt5  # type: ignore
    _MT5_IMPORT_ERROR = ""
except Exception as exc:  # pragma: no cover - depends on host platform
    _mt5 = None
    _MT5_IMPORT_ERROR = str(exc)


# MT5 timeframe constants, resolved lazily against the imported module. The
# canonical labels map to attributes on the MetaTrader5 module.
_TIMEFRAME_ATTR = {
    "M1": "TIMEFRAME_M1",
    "M5": "TIMEFRAME_M5",
    "M15": "TIMEFRAME_M15",
    "M30": "TIMEFRAME_M30",
    "H1": "TIMEFRAME_H1",
    "H4": "TIMEFRAME_H4",
    "D1": "TIMEFRAME_D1",
    "W1": "TIMEFRAME_W1",
    "MN1": "TIMEFRAME_MN1",
}


@dataclass
class Candle:
    time: int
    open: float
    high: float
    low: float
    close: float
    tick_volume: int = 0
    spread: int = 0
    real_volume: int = 0

    def to_dict(self) -> Dict[str, Any]:
        return {
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
class ClientResult:
    """Result of a client operation; explicit success flag, never fabricated."""

    ok: bool
    data: Dict[str, Any] = field(default_factory=dict)
    error_code: str = ""
    message: str = ""


class Mt5Client:
    def __init__(self) -> None:
        self._initialized = False
        self._resolved_symbol = ""
        self._broker = ""
        self._server = ""
        self._terminal_version: Optional[tuple] = None
        # Server wall clock, captured from the MT5 terminal's own timestamps.
        # Broker MT5 servers stamp bars in *server* time (commonly UTC+2/+3),
        # not UTC, so a live bar can appear to open "in the future" relative to
        # the host's UTC clock. `_clock_offset_seconds` is (server_now -
        # local_utc_now), estimated from the newest M1 bar open time; it is
        # applied to every reported age so ages are non-negative and the host
        # never rejects a real bar as future-dated. Zero until measured.
        self._clock_offset_seconds = 0
        self._clock_reference_epoch = 0
        # The MetaTrader5 Python API is not thread-safe. The bridge server is
        # threaded, so every terminal call is serialized through one lock to
        # prevent concurrent requests (chart, canonical candles, tick, symbol)
        # from corrupting the terminal session.
        self._mt5_lock = threading.Lock()

    # -- availability -------------------------------------------------------

    @property
    def available(self) -> bool:
        return _mt5 is not None

    @property
    def import_error(self) -> str:
        return _MT5_IMPORT_ERROR

    @property
    def resolved_symbol(self) -> str:
        return self._resolved_symbol

    @property
    def broker(self) -> str:
        return self._broker

    @property
    def clock_offset_seconds(self) -> int:
        return self._clock_offset_seconds

    # -- clock alignment ----------------------------------------------------

    def _server_now_epoch(self) -> Optional[int]:
        """Read the newest *forming* M1 bar's time, i.e. the server's current
        minute. Returns None when the terminal is unavailable. Caller must hold
        no MT5 lock unless it already owns one (this acquires it)."""
        if _mt5 is None:
            return None
        try:
            with self._mt5_lock:
                rates = _mt5.copy_rates_from_pos(
                    self._resolved_symbol or "XAUUSD", _mt5.TIMEFRAME_M1, 0, 1)
        except Exception:
            return None
        if rates is None or len(rates) == 0:
            return None
        try:
            return int(rates[0]["time"])
        except Exception:
            return None

    def _capture_clock_offset(self) -> None:
        """Estimate (server_now - local_utc_now) and remember it so `age_seconds`
        is computed against the server clock, never a skewed local one.

        The read is the newest *forming* M1 bar's open time (the server's current
        minute). The result is accepted only when it falls inside a plausible
        broker timezone band: a larger implied offset means the feed is stale
        (e.g. a weekend, or a synthetic stale feed), not that the server sits in
        an exotic timezone. Accepting it would subtract that staleness and mask a
        dead feed, which this system must never do. On a stale feed the offset
        stays 0 and staleness is reported honestly.
        """
        server_now = self._server_now_epoch()
        if server_now is None:
            return
        local_now = int(time.time())
        # Round to the nearest minute: the source is a minute-boundary bar open
        # time, so sub-minute noise is quantization, not real skew.
        offset = server_now - (local_now - (local_now % 60))
        if abs(offset) > _MAX_CLOCK_OFFSET_SECONDS:
            # Outside any real timezone: a stale feed, not clock skew.
            return
        self._clock_offset_seconds = offset
        self._clock_reference_epoch = local_now

    def clock_alignment(self) -> Dict[str, Any]:
        """Server-vs-UTC clock view for the health payload (diagnostics only)."""
        return {
            "server_clock_offset_seconds": self._clock_offset_seconds,
            "clock_reference_epoch": self._clock_reference_epoch,
        }


    # -- lifecycle ----------------------------------------------------------

    def initialize(self, path: str = "", timeout_ms: int = 0) -> ClientResult:
        """Attach to the MetaTrader 5 terminal.

        With no ``path`` the MT5 package finds the running terminal itself over
        IPC. ``path`` is only needed to target a specific installation, e.g. a
        broker that ships a suffixed terminal ("MetaTrader 5 IC Markets (SC)").

        ``timeout_ms`` bounds the IPC handshake. MT5's own default is 60000 ms,
        which is why an unreachable terminal looked like a hang rather than a
        reported error.
        """
        if _mt5 is None:
            return ClientResult(
                ok=False,
                error_code="MT5_TERMINAL_UNAVAILABLE",
                message=f"MetaTrader5 package unavailable: {_MT5_IMPORT_ERROR}",
            )
        kwargs: Dict[str, Any] = {}
        if timeout_ms and timeout_ms > 0:
            kwargs["timeout"] = int(timeout_ms)
        try:
            # MT5 takes the path as the first *positional* argument; passing it
            # by keyword is not supported by the package.
            ok = _mt5.initialize(path, **kwargs) if path else _mt5.initialize(**kwargs)
            if not ok:
                err = ""
                try:
                    err = str(_mt5.last_error())
                except Exception:
                    err = "unknown"
                detail = f"path={path!r} " if path else "auto-discovery "
                return ClientResult(
                    ok=False,
                    error_code="MT5_TERMINAL_UNAVAILABLE",
                    message=f"mt5.initialize failed ({detail}timeout={kwargs.get('timeout', 'default')}ms): {err}",
                )
        except Exception as exc:  # pragma: no cover
            return ClientResult(ok=False, error_code="MT5_TERMINAL_UNAVAILABLE", message=str(exc))

        self._initialized = True
        try:
            self._terminal_version = tuple(_mt5.version()) if _mt5.version() else None
        except Exception:
            self._terminal_version = None
        try:
            acc = _mt5.account_info()
            if acc is not None:
                self._broker = getattr(acc, "company", "") or ""
                self._server = getattr(acc, "server", "") or ""
        except Exception:
            pass
        return ClientResult(ok=True)

    def shutdown(self) -> None:
        if _mt5 is not None and self._initialized:
            try:
                _mt5.shutdown()
            except Exception:
                pass
        self._initialized = False

    # -- symbol resolution --------------------------------------------------

    def resolve_symbol(self, preferred: str = "XAUUSD") -> ClientResult:
        """Resolve the broker's XAUUSD representation.

        Preference order (mirrors the reference guide):
          1. exact `XAUUSD`
          2. any name beginning with `XAUUSD` (e.g. XAUUSD.vx)
          3. first symbol containing `XAU`
        The resolved name is recorded; we never pretend the bare name exists.
        """
        if _mt5 is None:
            return ClientResult(ok=False, error_code="MT5_TERMINAL_UNAVAILABLE",
                                message="MetaTrader5 package unavailable")
        try:
            symbols = _mt5.symbols_get()
        except Exception as exc:
            return ClientResult(ok=False, error_code="MT5_SYMBOL_UNRESOLVED", message=str(exc))
        if symbols is None:
            return ClientResult(ok=False, error_code="MT5_SYMBOL_UNRESOLVED",
                                message="symbols_get returned None")

        names = [s.name for s in symbols if "XAU" in s.name.upper()]
        if not names:
            return ClientResult(ok=False, error_code="MT5_SYMBOL_UNRESOLVED",
                                message="no XAU symbol available on this broker")

        chosen = ""
        for name in names:
            if name.upper() == preferred.upper():
                chosen = name
                break
        if not chosen:
            for name in names:
                if name.upper().startswith(preferred.upper()):
                    chosen = name
                    break
        if not chosen:
            chosen = names[0]

        try:
            _mt5.symbol_select(chosen, True)
        except Exception:
            pass
        self._resolved_symbol = chosen
        # The symbol is now known, so the server clock can be read from its M1
        # feed. Measure once here so every subsequent read reports server-time
        # ages from the first bar onward.
        self._capture_clock_offset()
        return ClientResult(ok=True, data={"symbol": chosen, "candidates": names})

    # -- market data --------------------------------------------------------

    def _timeframe_constant(self, label: str) -> Optional[int]:
        attr = _TIMEFRAME_ATTR.get(label)
        if attr is None or _mt5 is None:
            return None
        return getattr(_mt5, attr, None)

    def read_candles(self, symbol: str, timeframe: str, count: int,
                     closed_only: bool = True) -> ClientResult:
        """Read candles. With closed_only=True the forming bar is skipped.

        `start_pos=1` is the closed-bar guarantee: MT5 index 0 is the current
        forming bar and must never be used as a decision bar.
        """
        if _mt5 is None:
            return ClientResult(ok=False, error_code="MT5_TERMINAL_UNAVAILABLE",
                                message="MetaTrader5 package unavailable")
        if timeframe not in TIMEFRAMES:
            return ClientResult(ok=False, error_code="MARKET_DATA_INVALID",
                                message=f"unknown timeframe {timeframe}")
        if count <= 0:
            return ClientResult(ok=False, error_code="MARKET_DATA_INVALID",
                                message="count must be positive")

        tf_const = self._timeframe_constant(timeframe)
        if tf_const is None:
            return ClientResult(ok=False, error_code="MARKET_DATA_INVALID",
                                message=f"timeframe constant missing for {timeframe}")

        start_pos = 1 if closed_only else 0
        try:
            with self._mt5_lock:
                rates = _mt5.copy_rates_from_pos(symbol, tf_const, start_pos, count)
        except Exception as exc:
            return ClientResult(ok=False, error_code="MARKET_DATA_MISSING", message=str(exc))
        if rates is None or len(rates) == 0:
            return ClientResult(ok=False, error_code="MARKET_DATA_MISSING",
                                message=f"no candles for {symbol} {timeframe}")

        names = getattr(getattr(rates, "dtype", None), "names", None) or ()
        candles: List[Candle] = []
        for r in rates:
            candles.append(Candle(
                time=int(r["time"]),
                open=float(r["open"]),
                high=float(r["high"]),
                low=float(r["low"]),
                close=float(r["close"]),
                tick_volume=int(r["tick_volume"]) if "tick_volume" in names else 0,
                spread=int(r["spread"]) if "spread" in names else 0,
                real_volume=int(r["real_volume"]) if "real_volume" in names else 0,
            ))

        # Staleness: the newest *closed* bar should not be older than a few
        # nominal bar intervals. Ages are measured against the *server* clock
        # (local UTC + the measured broker offset), because MT5 stamps bars in
        # broker server time; comparing them to a raw local UTC clock made every
        # recent bar look future-dated (negative age) and had the host reject it.
        now = int(time.time()) + self._clock_offset_seconds
        interval = _TIMEFRAME_SECONDS.get(timeframe, 0)
        newest_closed = candles[-1].time
        age_seconds = now - newest_closed
        if interval <= 0:
            freshness = "UNKNOWN"
        elif age_seconds > interval * _STALE_INTERVAL_MULTIPLIER:
            freshness = "STALE"
        else:
            freshness = "FRESH"

        return ClientResult(ok=True, data={
            "symbol": symbol,
            "timeframe": timeframe,
            "closed_only": closed_only,
            "count": len(candles),
            "newest_closed_time": newest_closed,
            "age_seconds": age_seconds,
            "freshness": freshness,
            "clock_offset_seconds": self._clock_offset_seconds,
            "candles": [c.to_dict() for c in candles],
        })

    def read_tick(self, symbol: str) -> ClientResult:
        if _mt5 is None:
            return ClientResult(ok=False, error_code="MT5_TERMINAL_UNAVAILABLE",
                                message="MetaTrader5 package unavailable")
        try:
            with self._mt5_lock:
                tick = _mt5.symbol_info_tick(symbol)
        except Exception as exc:
            return ClientResult(ok=False, error_code="MARKET_DATA_MISSING", message=str(exc))
        if tick is None:
            return ClientResult(ok=False, error_code="MARKET_DATA_MISSING",
                                message=f"no tick for {symbol}")
        return ClientResult(ok=True, data={
            "symbol": symbol,
            "time": int(getattr(tick, "time", 0)),
            "bid": float(getattr(tick, "bid", 0.0)),
            "ask": float(getattr(tick, "ask", 0.0)),
            "last": float(getattr(tick, "last", 0.0)),
            "volume": float(getattr(tick, "volume", 0.0)),
            "flags": int(getattr(tick, "flags", 0)),
        })

    def symbol_specification(self, symbol: str) -> ClientResult:
        if _mt5 is None:
            return ClientResult(ok=False, error_code="MT5_TERMINAL_UNAVAILABLE",
                                message="MetaTrader5 package unavailable")
        try:
            with self._mt5_lock:
                info = _mt5.symbol_info(symbol)
        except Exception as exc:
            return ClientResult(ok=False, error_code="MT5_SYMBOL_UNRESOLVED", message=str(exc))
        if info is None:
            return ClientResult(ok=False, error_code="MT5_SYMBOL_UNRESOLVED",
                                message=f"no symbol_info for {symbol}")
        fields = ["name", "digits", "point", "trade_tick_size", "trade_tick_value",
                  "trade_contract_size", "volume_min", "volume_max", "volume_step",
                  "trade_stops_level", "trade_freeze_level", "trade_mode",
                  "filling_mode", "margin_mode"]
        data: Dict[str, Any] = {}
        for f in fields:
            if hasattr(info, f):
                data[f] = getattr(info, f)
        return ClientResult(ok=True, data=data)

    # -- identity / health --------------------------------------------------

    def health(self) -> Dict[str, Any]:
        return {
            "package_available": self.available,
            "import_error": self.import_error,
            "initialized": self._initialized,
            "terminal_version": list(self._terminal_version) if self._terminal_version else None,
            "broker": self._broker,
            "server": self._server,
            "resolved_symbol": self._resolved_symbol,
            "server_clock_offset_seconds": self._clock_offset_seconds,
        }
