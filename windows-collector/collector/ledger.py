"""Delivery ledger: what we sent, and what the receiver confirmed.

Two facts are tracked separately and never conflated:

  * the candle store holds "what we have";
  * this ledger holds "what the receiver has acknowledged".

A candle is only marked acknowledged after the receiver explicitly confirms it
persisted that exact unique id. An HTTP 200 is not enough — a response that does
not name the candle proves nothing about it.

States, per candle id:

  pending       queued for delivery, request not yet confirmed
  acknowledged  receiver confirmed persistence of this exact id
  rejected      receiver refused the candle (bad data); never auto-retried

The key includes the destination, so switching the receiving endpoint cannot
make candles look delivered to a receiver that has never seen them. That
failure mode — a silent gap after a destination change — is the reason the
destination is part of the key rather than a column beside it.
"""

from __future__ import annotations

import os
import sqlite3
import threading
import time
from dataclasses import dataclass
from typing import Any, Dict, List, Optional

from .identity import CandleKey

SCHEMA_VERSION = "1.0"

STATE_PENDING = "pending"
STATE_ACKNOWLEDGED = "acknowledged"
STATE_REJECTED = "rejected"


@dataclass(frozen=True)
class LedgerEntry:
    destination: str
    uid: str
    symbol: str
    timeframe: str
    open_time_utc: int
    state: str
    attempts: int = 0
    last_attempt_at: int = 0
    acked_at: int = 0
    detail: str = ""

    @property
    def key(self) -> CandleKey:
        return CandleKey(self.symbol, self.timeframe, self.open_time_utc)


class DeliveryLedger:
    """Durable per-destination delivery state, keyed by candle unique id."""

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
            "CREATE TABLE IF NOT EXISTS deliveries ("
            "  destination    TEXT NOT NULL,"   # receiving endpoint identity
            "  uid            TEXT NOT NULL,"   # deterministic candle id
            "  symbol         TEXT NOT NULL,"
            "  timeframe      TEXT NOT NULL,"
            "  open_time_utc  INTEGER NOT NULL,"
            "  state          TEXT NOT NULL,"
            "  attempts       INTEGER NOT NULL DEFAULT 0,"
            "  last_attempt_at INTEGER NOT NULL DEFAULT 0,"
            "  acked_at       INTEGER NOT NULL DEFAULT 0,"
            "  detail         TEXT NOT NULL DEFAULT '',"
            "  PRIMARY KEY (destination, uid)"
            ");"
        )
        self._connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_deliveries_stream "
            "ON deliveries(destination, symbol, timeframe, open_time_utc);"
        )
        self._connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_deliveries_state "
            "ON deliveries(destination, state);"
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

    # -- state transitions --------------------------------------------------

    def record_pending(self, destination: str, key: CandleKey) -> None:
        """Queue a candle for delivery. Idempotent: an already-acknowledged
        candle is left alone, so a re-queue cannot undo a confirmation."""
        with self._mutex:
            self._connection.execute(
                "INSERT INTO deliveries(destination, uid, symbol, timeframe,"
                " open_time_utc, state, attempts, last_attempt_at, acked_at,"
                " detail)"
                " VALUES (?,?,?,?,?,?,0,?,0,'')"
                " ON CONFLICT(destination, uid) DO UPDATE SET"
                "   state=CASE WHEN deliveries.state=? THEN deliveries.state"
                "              ELSE excluded.state END,"
                "   last_attempt_at=excluded.last_attempt_at",
                (destination, key.uid, key.symbol, key.timeframe,
                 key.open_time_utc, STATE_PENDING, int(time.time()),
                 STATE_ACKNOWLEDGED),
            )
            self._connection.commit()

    def record_attempt(self, destination: str, key: CandleKey) -> None:
        """Count a delivery attempt. Used for bounded backoff and reporting."""
        with self._mutex:
            self._connection.execute(
                "UPDATE deliveries SET attempts=attempts+1, last_attempt_at=?"
                " WHERE destination=? AND uid=?",
                (int(time.time()), destination, key.uid))
            self._connection.commit()

    def record_acknowledged(self, destination: str, key: CandleKey,
                            detail: str = "") -> None:
        """The receiver explicitly confirmed persistence of this exact id."""
        with self._mutex:
            self._connection.execute(
                "INSERT INTO deliveries(destination, uid, symbol, timeframe,"
                " open_time_utc, state, attempts, last_attempt_at, acked_at,"
                " detail)"
                " VALUES (?,?,?,?,?,?,0,?,?,?)"
                " ON CONFLICT(destination, uid) DO UPDATE SET"
                "   state=excluded.state, acked_at=excluded.acked_at,"
                "   detail=excluded.detail",
                (destination, key.uid, key.symbol, key.timeframe,
                 key.open_time_utc, STATE_ACKNOWLEDGED, int(time.time()),
                 int(time.time()), detail))
            self._connection.commit()

    def record_rejected(self, destination: str, key: CandleKey,
                        detail: str = "") -> None:
        """The receiver refused the candle. Never auto-retried: a rejected
        candle is bad data, and retrying it would loop forever."""
        with self._mutex:
            self._connection.execute(
                "INSERT INTO deliveries(destination, uid, symbol, timeframe,"
                " open_time_utc, state, attempts, last_attempt_at, acked_at,"
                " detail)"
                " VALUES (?,?,?,?,?,?,0,?,0,?)"
                " ON CONFLICT(destination, uid) DO UPDATE SET"
                "   state=excluded.state, detail=excluded.detail",
                (destination, key.uid, key.symbol, key.timeframe,
                 key.open_time_utc, STATE_REJECTED, int(time.time()), detail))
            self._connection.commit()

    # -- queries ------------------------------------------------------------

    def state(self, destination: str, key: CandleKey) -> str:
        with self._mutex:
            row = self._connection.execute(
                "SELECT state FROM deliveries WHERE destination=? AND uid=?",
                (destination, key.uid)).fetchone()
        return row[0] if row else ""

    def is_acknowledged(self, destination: str, key: CandleKey) -> bool:
        return self.state(destination, key) == STATE_ACKNOWLEDGED

    def acknowledged_open_times(self, destination: str, symbol: str,
                                timeframe: str,
                                start_utc: Optional[int] = None,
                                end_utc: Optional[int] = None) -> List[int]:
        """Ascending open times the receiver confirmed for one stream.

        Returned as a set of instants, not a single watermark, so a hole in the
        middle is representable.
        """
        query = ("SELECT open_time_utc FROM deliveries"
                 " WHERE destination=? AND symbol=? AND timeframe=? AND state=?")
        params: List[Any] = [destination, symbol, timeframe, STATE_ACKNOWLEDGED]
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

    def latest_acknowledged(self, destination: str, symbol: str,
                            timeframe: str) -> Optional[int]:
        with self._mutex:
            row = self._connection.execute(
                "SELECT MAX(open_time_utc) FROM deliveries"
                " WHERE destination=? AND symbol=? AND timeframe=? AND state=?",
                (destination, symbol, timeframe, STATE_ACKNOWLEDGED)).fetchone()
        return int(row[0]) if row and row[0] is not None else None

    def pending(self, destination: str,
                limit: int = 0) -> List[LedgerEntry]:
        """Unconfirmed deliveries, oldest candle first."""
        query = ("SELECT destination, uid, symbol, timeframe, open_time_utc,"
                 " state, attempts, last_attempt_at, acked_at, detail"
                 " FROM deliveries WHERE destination=? AND state=?"
                 " ORDER BY open_time_utc ASC")
        params: List[Any] = [destination, STATE_PENDING]
        if limit > 0:
            query += " LIMIT ?"
            params.append(int(limit))
        with self._mutex:
            rows = self._connection.execute(query, tuple(params)).fetchall()
        return [LedgerEntry(*row[:5], row[5], int(row[6]), int(row[7]),
                            int(row[8]), row[9]) for row in rows]

    def rejected(self, destination: str) -> List[LedgerEntry]:
        with self._mutex:
            rows = self._connection.execute(
                "SELECT destination, uid, symbol, timeframe, open_time_utc,"
                " state, attempts, last_attempt_at, acked_at, detail"
                " FROM deliveries WHERE destination=? AND state=?"
                " ORDER BY open_time_utc ASC",
                (destination, STATE_REJECTED)).fetchall()
        return [LedgerEntry(*row[:5], row[5], int(row[6]), int(row[7]),
                            int(row[8]), row[9]) for row in rows]

    def counts(self, destination: str = "") -> Dict[str, int]:
        query = "SELECT state, COUNT(*) FROM deliveries"
        params: List[Any] = []
        if destination:
            query += " WHERE destination=?"
            params.append(destination)
        query += " GROUP BY state"
        with self._mutex:
            rows = self._connection.execute(query, tuple(params)).fetchall()
        out = {STATE_PENDING: 0, STATE_ACKNOWLEDGED: 0, STATE_REJECTED: 0}
        for state, count in rows:
            out[state] = int(count)
        return out

    def clear_destination(self, destination: str) -> int:
        """Forget all delivery state for one destination.

        Used only by an explicit operator action (re-send everything to this
        destination). Never called automatically: doing so would discard the
        record of what the receiver already has.
        """
        with self._mutex:
            cursor = self._connection.execute(
                "DELETE FROM deliveries WHERE destination=?", (destination,))
            self._connection.commit()
            return cursor.rowcount

    # -- helpers ------------------------------------------------------------

    @staticmethod
    def destination_id(backend_url: str) -> str:
        """Stable identity for a receiving endpoint.

        Derived from the normalized URL so the same endpoint keeps the same id
        across restarts, and a different endpoint gets a different id.
        """
        url = (backend_url or "").strip().rstrip("/")
        lower = url.lower()
        for prefix in ("https://", "http://"):
            if lower.startswith(prefix):
                lower = lower[len(prefix):]
                break
        return lower or "unset"

    def close(self) -> None:
        with self._mutex:
            self._connection.close()

    def __enter__(self) -> "DeliveryLedger":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()
