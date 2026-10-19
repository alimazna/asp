"""Local candle store: one durable file holding every candle we have seen.

This is the source of truth for "what do we have". Delivery state is tracked
separately (see ``ledger.py``), because "we have the candle" and "the receiver
confirmed the candle" are different facts and conflating them is how a gap
becomes invisible.

Layout: a single SQLite file with one row per candle, keyed by the unique id
from ``identity.py``. The primary key is the unique id, so inserting the same
candle twice is a no-op — the store cannot hold a duplicate even if the caller
re-reads the same bar from MetaTrader on every poll.

Ordering: every query that feeds delivery returns rows in
``(open_time_utc, timeframe_rank)`` order, so a caller cannot accidentally send
a later candle before an earlier one.
"""

from __future__ import annotations

import os
import sqlite3
import threading
from dataclasses import dataclass
from typing import Any, Dict, Iterable, List, Optional, Sequence

from .identity import (
    CandleKey,
    candle_uid,
    is_known_timeframe,
    timeframe_rank,
)

SCHEMA_VERSION = "1.0"


@dataclass(frozen=True)
class StoredCandle:
    """One closed candle as held locally."""

    uid: str
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
    source: str = ""            # broker/server identifier, when known
    stored_at: int = 0          # local write time (UTC epoch seconds)

    @property
    def key(self) -> CandleKey:
        return CandleKey(self.symbol, self.timeframe, self.open_time_utc)

    def to_payload(self) -> Dict[str, Any]:
        """Wire form. The unique id travels with the candle so the receiver can
        verify identity without recomputing it."""
        return {
            "uid": self.uid,
            "symbol": self.symbol,
            "timeframe": self.timeframe,
            "open_time_utc": self.open_time_utc,
            "open": self.open,
            "high": self.high,
            "low": self.low,
            "close": self.close,
            "tick_volume": self.tick_volume,
            "spread": self.spread,
            "real_volume": self.real_volume,
            "source": self.source,
        }


class CandleStore:
    """Durable single-file store for every candle the collector has read."""

    def __init__(self, db_path: str):
        self._path = db_path
        self._mutex = threading.RLock()
        directory = os.path.dirname(os.path.abspath(db_path))
        if directory:
            os.makedirs(directory, exist_ok=True)
        self._connection = sqlite3.connect(db_path, check_same_thread=False)
        self._connection.execute("PRAGMA journal_mode=WAL;")
        self._connection.execute("PRAGMA synchronous=NORMAL;")
        self._connection.execute(
            "CREATE TABLE IF NOT EXISTS candles ("
            "  uid            TEXT PRIMARY KEY,"   # deterministic unique id
            "  symbol         TEXT NOT NULL,"
            "  timeframe      TEXT NOT NULL,"
            "  open_time_utc  INTEGER NOT NULL,"
            "  open           REAL NOT NULL,"
            "  high           REAL NOT NULL,"
            "  low            REAL NOT NULL,"
            "  close          REAL NOT NULL,"
            "  tick_volume    INTEGER NOT NULL DEFAULT 0,"
            "  spread         INTEGER NOT NULL DEFAULT 0,"
            "  real_volume    INTEGER NOT NULL DEFAULT 0,"
            "  source         TEXT NOT NULL DEFAULT '',"
            "  stored_at      INTEGER NOT NULL DEFAULT 0"
            ");"
        )
        # The delivery walk always reads one timeframe in time order, so index
        # on exactly that shape.
        self._connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_candles_stream "
            "ON candles(symbol, timeframe, open_time_utc);"
        )
        self._connection.execute(
            "CREATE TABLE IF NOT EXISTS meta ("
            "  key TEXT PRIMARY KEY, value TEXT NOT NULL"
            ");"
        )
        self._connection.execute(
            "INSERT OR IGNORE INTO meta(key, value) VALUES ('schema_version', ?)",
            (SCHEMA_VERSION,),
        )
        self._connection.commit()

    # -- writes -------------------------------------------------------------

    def put(self, candle: StoredCandle) -> bool:
        """Insert a candle. Returns True when it was new, False when it already
        existed. A repeat write never mutates the stored row, so a candle is
        immutable once captured — a re-read cannot rewrite history."""
        with self._mutex:
            cursor = self._connection.execute(
                "INSERT OR IGNORE INTO candles("
                " uid, symbol, timeframe, open_time_utc, open, high, low, close,"
                " tick_volume, spread, real_volume, source, stored_at)"
                " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
                (candle.uid, candle.symbol, candle.timeframe,
                 candle.open_time_utc, candle.open, candle.high, candle.low,
                 candle.close, candle.tick_volume, candle.spread,
                 candle.real_volume, candle.source, candle.stored_at),
            )
            self._connection.commit()
            return cursor.rowcount == 1

    def put_many(self, candles: Iterable[StoredCandle]) -> int:
        """Insert many candles in one transaction. Returns the count that were
        new. Atomic: a failure leaves the store unchanged."""
        inserted = 0
        with self._mutex:
            try:
                self._connection.execute("BEGIN")
                for candle in candles:
                    cursor = self._connection.execute(
                        "INSERT OR IGNORE INTO candles("
                        " uid, symbol, timeframe, open_time_utc, open, high,"
                        " low, close, tick_volume, spread, real_volume, source,"
                        " stored_at)"
                        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
                        (candle.uid, candle.symbol, candle.timeframe,
                         candle.open_time_utc, candle.open, candle.high,
                         candle.low, candle.close, candle.tick_volume,
                         candle.spread, candle.real_volume, candle.source,
                         candle.stored_at),
                    )
                    if cursor.rowcount == 1:
                        inserted += 1
                self._connection.commit()
            except Exception:
                self._connection.rollback()
                raise
        return inserted

    # -- reads --------------------------------------------------------------

    def get(self, uid: str) -> Optional[StoredCandle]:
        with self._mutex:
            row = self._connection.execute(
                "SELECT uid, symbol, timeframe, open_time_utc, open, high, low,"
                " close, tick_volume, spread, real_volume, source, stored_at"
                " FROM candles WHERE uid=?", (uid,)).fetchone()
        return self._row_to_candle(row) if row else None

    def has(self, uid: str) -> bool:
        with self._mutex:
            row = self._connection.execute(
                "SELECT 1 FROM candles WHERE uid=?", (uid,)).fetchone()
        return row is not None

    def count(self, symbol: str = "", timeframe: str = "") -> int:
        query = "SELECT COUNT(*) FROM candles"
        clauses: List[str] = []
        params: List[Any] = []
        if symbol:
            clauses.append("symbol=?")
            params.append(symbol)
        if timeframe:
            clauses.append("timeframe=?")
            params.append(timeframe)
        if clauses:
            query += " WHERE " + " AND ".join(clauses)
        with self._mutex:
            row = self._connection.execute(query, tuple(params)).fetchone()
        return int(row[0]) if row else 0

    def open_times(self, symbol: str, timeframe: str,
                   start_utc: Optional[int] = None,
                   end_utc: Optional[int] = None) -> List[int]:
        """All stored open times for one stream, ascending.

        This is the set used for gap comparison against the receiver: a set of
        instants, not a single watermark, so a hole in the middle is visible.
        """
        query = ("SELECT open_time_utc FROM candles WHERE symbol=? AND timeframe=?")
        params: List[Any] = [symbol, timeframe]
        if start_utc is not None:
            query += " AND open_time_utc>=?"
            params.append(int(start_utc))
        if end_utc is not None:
            query += " AND open_time_utc<=?"
            params.append(int(end_utc))
        query += " ORDER BY open_time_utc ASC"
        with self._mutex:
            rows = self._connection.execute(query, tuple(params)).fetchall()
        return [int(r[0]) for r in rows]

    def recent(self, symbol: str, timeframe: str,
               limit: int = 500) -> List[StoredCandle]:
        """Newest ``limit`` candles for one stream, returned oldest-first so the
        caller can deliver them in order without re-sorting."""
        with self._mutex:
            rows = self._connection.execute(
                "SELECT uid, symbol, timeframe, open_time_utc, open, high, low,"
                " close, tick_volume, spread, real_volume, source, stored_at"
                " FROM candles WHERE symbol=? AND timeframe=?"
                " ORDER BY open_time_utc DESC LIMIT ?",
                (symbol, timeframe, int(limit))).fetchall()
        candles = [self._row_to_candle(r) for r in rows]
        candles.reverse()
        return candles

    def range(self, symbol: str, timeframe: str, start_utc: int,
              end_utc: int) -> List[StoredCandle]:
        """Candles in ``[start_utc, end_utc]``, oldest-first."""
        with self._mutex:
            rows = self._connection.execute(
                "SELECT uid, symbol, timeframe, open_time_utc, open, high, low,"
                " close, tick_volume, spread, real_volume, source, stored_at"
                " FROM candles WHERE symbol=? AND timeframe=?"
                "   AND open_time_utc>=? AND open_time_utc<=?"
                " ORDER BY open_time_utc ASC",
                (symbol, timeframe, int(start_utc), int(end_utc))).fetchall()
        return [self._row_to_candle(r) for r in rows]

    def latest_open_time(self, symbol: str,
                         timeframe: str) -> Optional[int]:
        with self._mutex:
            row = self._connection.execute(
                "SELECT MAX(open_time_utc) FROM candles"
                " WHERE symbol=? AND timeframe=?",
                (symbol, timeframe)).fetchone()
        return int(row[0]) if row and row[0] is not None else None

    def earliest_open_time(self, symbol: str,
                           timeframe: str) -> Optional[int]:
        with self._mutex:
            row = self._connection.execute(
                "SELECT MIN(open_time_utc) FROM candles"
                " WHERE symbol=? AND timeframe=?",
                (symbol, timeframe)).fetchone()
        return int(row[0]) if row and row[0] is not None else None

    def streams(self, symbol: str = "") -> List[tuple]:
        """(symbol, timeframe, count, earliest, latest) for every stream held."""
        query = ("SELECT symbol, timeframe, COUNT(*), MIN(open_time_utc),"
                 " MAX(open_time_utc) FROM candles")
        params: List[Any] = []
        if symbol:
            query += " WHERE symbol=?"
            params.append(symbol)
        query += " GROUP BY symbol, timeframe"
        with self._mutex:
            rows = self._connection.execute(query, tuple(params)).fetchall()
        out = []
        for symbol_name, timeframe, count, earliest, latest in rows:
            out.append((symbol_name, timeframe, int(count), int(earliest),
                        int(latest)))
        out.sort(key=lambda item: (item[0], timeframe_rank(item[1])))
        return out

    # -- helpers ------------------------------------------------------------

    @staticmethod
    def _row_to_candle(row: Sequence[Any]) -> StoredCandle:
        return StoredCandle(
            uid=row[0], symbol=row[1], timeframe=row[2],
            open_time_utc=int(row[3]), open=float(row[4]), high=float(row[5]),
            low=float(row[6]), close=float(row[7]), tick_volume=int(row[8]),
            spread=int(row[9]), real_volume=int(row[10]), source=row[11],
            stored_at=int(row[12]))

    @staticmethod
    def make_uid(symbol: str, timeframe: str, open_time_utc: int) -> str:
        return candle_uid(symbol, timeframe, open_time_utc)

    def close(self) -> None:
        with self._mutex:
            self._connection.close()

    def __enter__(self) -> "CandleStore":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()


def validate_candle(symbol: str, timeframe: str, open_time_utc: int,
                    open_: float, high: float, low: float, close: float,
                    now_utc: int, clock_offset_seconds: int = 0) -> Optional[str]:
    """Reject a candle that cannot be trusted. Returns a reason, or None.

    Mirrors the checks the receiver performs so a bad bar is caught locally
    rather than rejected mid-delivery. ``clock_offset_seconds`` is the measured
    (broker - UTC) offset: MetaTrader stamps bar open times in broker server
    time, so the future-dating guard must run in that same frame or every
    intraday bar from a UTC+3 broker looks future-dated.
    """
    if not symbol:
        return "symbol is empty"
    if not is_known_timeframe(timeframe):
        return f"unknown timeframe: {timeframe!r}"
    if open_time_utc < 0:
        return "open time is negative"
    for name, value in (("open", open_), ("high", high), ("low", low),
                        ("close", close)):
        if value != value:  # NaN
            return f"{name} is NaN"
        if value <= 0:
            return f"{name} must be positive"
    if high < low:
        return f"high ({high}) is below low ({low})"
    if high < max(open_, close) or low > min(open_, close):
        return (f"OHLC inconsistent: high {high} / low {low} do not bracket "
                f"open {open_} / close {close}")
    latest_allowed = now_utc + clock_offset_seconds + 60
    if open_time_utc > latest_allowed:
        return (f"open time {open_time_utc} is in the future "
                f"(allowed up to {latest_allowed})")
    return None
