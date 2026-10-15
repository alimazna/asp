"""Durable local acknowledgment ledger (SQLite).

Tracks delivery state per candle identity
    (source_id, symbol, timeframe, open_time_utc)
so that:
  * a candle confirmed by the server is never re-sent in normal operation;
  * uncertain deliveries (timeout / lost response) stay PENDING and are
    retried with the same idempotency identity;
  * restarts of the sender, MT5, or the network never lose acknowledged state;
  * a malformed local record cannot corrupt unrelated candle state.

Identity is derived from source identity + symbol + timeframe + open time —
never from price values. Secrets are not stored in the ledger.
"""

from __future__ import annotations

import json
import os
import sqlite3
import threading
from typing import Any, Dict, List, Optional


SCHEMA_VERSION = "1.0"

_STATE_ACKNOWLEDGED = "acknowledged"
_STATE_PENDING = "pending"
_STATE_FAILED = "failed"


class DeliveryLedger:
    def __init__(self, db_path: str):
        self._path = db_path
        self._mutex = threading.Lock()
        directory = os.path.dirname(db_path)
        if directory:
            os.makedirs(directory, exist_ok=True)
        self._connection = sqlite3.connect(db_path, check_same_thread=False)
        self._connection.execute("PRAGMA journal_mode=WAL;")
        self._connection.execute(
            "CREATE TABLE IF NOT EXISTS deliveries ("
            "  source_id  TEXT NOT NULL,"
            "  symbol     TEXT NOT NULL,"
            "  timeframe  TEXT NOT NULL,"
            "  open_time  INTEGER NOT NULL,"
            "  state      TEXT NOT NULL,"
            "  identity   TEXT NOT NULL,"          # stable idempotency key
            "  received_at INTEGER NOT NULL,"      # server-ack time (UTC epoch)
            "  detail     TEXT NOT NULL DEFAULT '',"
            "  PRIMARY KEY (source_id, symbol, timeframe, open_time)"
            ");"
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

    # -- idempotency identity -----------------------------------------------

    @staticmethod
    def make_identity(source_id: str, symbol: str, timeframe: str,
                      open_time_utc: int) -> str:
        """Stable digest of the identity, independent of price values."""
        raw = f"{source_id}|{symbol}|{timeframe}|{open_time_utc}"
        digest = 0x811C9DC5  # FNV-1a 32-bit, deterministic, no crypto needed
        for byte in raw.encode("utf-8"):
            digest ^= byte
            digest = (digest * 0x01000193) & 0xFFFFFFFF
        return f"{raw}#{digest:08x}"

    # -- state queries ------------------------------------------------------

    def get_state(self, source_id: str, symbol: str, timeframe: str,
                  open_time_utc: int) -> Dict[str, Any]:
        with self._mutex:
            cursor = self._connection.execute(
                "SELECT state, identity, received_at, detail FROM deliveries"
                " WHERE source_id=? AND symbol=? AND timeframe=? AND open_time=?",
                (source_id, symbol, timeframe, open_time_utc),
            )
            row = cursor.fetchone()
            if row is None:
                return {"state": "", "identity": ""}
            return {"state": row[0], "identity": row[1]}

    def is_acknowledged(self, source_id: str, symbol: str, timeframe: str,
                        open_time_utc: int) -> bool:
        return self.get_state(source_id, symbol, timeframe,
                              open_time_utc)["state"] == _STATE_ACKNOWLEDGED

    def latest_acknowledged(self, source_id: str, symbol: str,
                            timeframe: str) -> Optional[int]:
        """Newest acked open time for the identity set, or None."""
        with self._mutex:
            cursor = self._connection.execute(
                "SELECT MAX(open_time) FROM deliveries"
                " WHERE source_id=? AND symbol=? AND timeframe=?"
                "   AND state=?",
                (source_id, symbol, timeframe, _STATE_ACKNOWLEDGED),
            )
            row = cursor.fetchone()
            return row[0] if row and row[0] is not None else None

    # -- transitions --------------------------------------------------------

    def record_pending(self, source_id: str, symbol: str, timeframe: str,
                       open_time_utc: int, identity: str) -> None:
        """Mark a candle queued for delivery (before the request is sent)."""
        with self._mutex:
            self._connection.execute(
                "INSERT INTO deliveries(source_id, symbol, timeframe, open_time,"
                " state, identity, received_at, detail)"
                " VALUES (?,?,?,?,?,?,?,?)"
                " ON CONFLICT(source_id, symbol, timeframe, open_time)"
                " DO UPDATE SET state=excluded.state, detail='' "
                " WHERE state=excluded.state",
                (source_id, symbol, timeframe, open_time_utc, _STATE_PENDING,
                 identity, 0, ""),
            )
            self._connection.commit()

    def record_acknowledged(self, source_id: str, symbol: str, timeframe: str,
                            open_time_utc: int, identity: str,
                            server_received_at: int) -> None:
        """Record the server's explicit confirmation (durable)."""
        with self._mutex:
            self._connection.execute(
                "INSERT INTO deliveries(source_id, symbol, timeframe, open_time,"
                " state, identity, received_at, detail)"
                " VALUES (?,?,?,?,?,?,?,?)"
                " ON CONFLICT(source_id, symbol, timeframe, open_time)"
                " DO UPDATE SET state=excluded.state,"
                "               identity=excluded.identity,"
                "               received_at=excluded.received_at,"
                "               detail=''",
                (source_id, symbol, timeframe, open_time_utc,
                 _STATE_ACKNOWLEDGED, identity, server_received_at, ""),
            )
            self._connection.commit()

    def record_failed(self, source_id: str, symbol: str, timeframe: str,
                      open_time_utc: int, detail: str,
                      identity: str = "") -> None:
        """Record a terminal rejection (not merely a timeout)."""
        state = self.get_state(source_id, symbol, timeframe, open_time_utc)
        current_identity = identity or state.get("identity", "")
        with self._mutex:
            self._connection.execute(
                "INSERT INTO deliveries(source_id, symbol, timeframe, open_time,"
                " state, identity, received_at, detail)"
                " VALUES (?,?,?,?,?,?,?,?)"
                " ON CONFLICT(source_id, symbol, timeframe, open_time)"
                " DO UPDATE SET state=excluded.state,"
                "               detail=excluded.detail",
                (source_id, symbol, timeframe, open_time_utc, _STATE_FAILED,
                 current_identity, 0, detail[:2000]),
            )
            self._connection.commit()

    # -- catch-up / gap detection -------------------------------------------

    def pending_and_unacked(self, source_id: str, symbol: str, timeframe: str,
                            open_times: List[int]) -> List[int]:
        """Filter `open_times` to those the ledger has NOT acknowledged.

        Also returns PENDING entries so an uncertain delivery is retried with
        its original identity and is never dropped during catch-up.
        """
        with self._mutex:
            out: List[int] = []
            for t in open_times:
                row = self._connection.execute(
                    "SELECT state FROM deliveries"
                    " WHERE source_id=? AND symbol=? AND timeframe=? AND open_time=?",
                    (source_id, symbol, timeframe, t),
                ).fetchone()
                if row is None or row[0] != _STATE_ACKNOWLEDGED:
                    out.append(t)
            return out

    def pending_identities(self) -> List[Dict[str, Any]]:
        """All PENDING deliveries (uncertain / crashed mid-flight), for replay."""
        with self._mutex:
            cursor = self._connection.execute(
                "SELECT source_id, symbol, timeframe, open_time, identity"
                " FROM deliveries WHERE state=?",
                (_STATE_PENDING,),
            )
            return [
                {"source_id": r[0], "symbol": r[1], "timeframe": r[2],
                 "open_time": r[3], "identity": r[4]}
                for r in cursor.fetchall()
            ]

    def counts(self) -> Dict[str, int]:
        with self._mutex:
            ack = self._connection.execute(
                "SELECT COUNT(*) FROM deliveries WHERE state=?",
                (_STATE_ACKNOWLEDGED,),
            ).fetchone()[0]
            pending = self._connection.execute(
                "SELECT COUNT(*) FROM deliveries WHERE state=?",
                (_STATE_PENDING,),
            ).fetchone()[0]
            failed = self._connection.execute(
                "SELECT COUNT(*) FROM deliveries WHERE state=?",
                (_STATE_FAILED,),
            ).fetchone()[0]
            return {
                "acknowledged": int(ack),
                "pending": int(pending),
                "failed": int(failed),
                "total": int(ack + pending + failed),
            }

    def close(self) -> None:
        with self._mutex:
            self._connection.commit()
            self._connection.close()