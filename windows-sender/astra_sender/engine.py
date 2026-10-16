"""Sender sync engine.

The engine wires the collector, the ledger, and the server client into the
closed-candle sync loop:

  1. read the latest *closed* candle per timeframe;
  2. skip candles the ledger has already acknowledged;
  3. send unacknowledged candles to the authenticated ingestion endpoint;
  4. validate the server response (candle identity + stored state) before
     recording the acknowledgment locally;
  5. on timeout / network failure keep the batch PENDING and retry with
     bounded exponential backoff and jitter;
  6. restart cleanly because the ledger is durable and the identity is
     stable.

The engine never fabricates candles and never submits the forming bar.
"""

from __future__ import annotations

import random
import threading
import time
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional

from .collector import ClosedCandleCollector
from .ledger import DeliveryLedger
from .protocol import ProtocolError, ServerClient


@dataclass
class EngineStatus:
    """Thread-safe snapshot exposed to the GUI."""

    running: bool = False
    mt5_state: str = "OFFLINE"          # OFFLINE | STARTING | READY | UNAVAILABLE
    server_state: str = "OFFLINE"       # OFFLINE | STARTING | ONLINE | AUTH_FAILED | ERROR
    mt5_error_code: str = ""            # stable code, e.g. MT5_TERMINAL_UNAVAILABLE
    server_error_code: str = ""
    resolved_symbol: str = ""
    last_error: str = ""
    last_sync_at: int = 0                # UTC epoch of last good loop
    sent: int = 0
    acknowledged: int = 0
    deduplicated: int = 0
    queued: int = 0
    failed: int = 0
    last_candle_per_timeframe: Dict[str, Dict[str, Any]] = field(default_factory=dict)
    acked_candle_per_timeframe: Dict[str, Dict[str, Any]] = field(default_factory=dict)

    def snapshot(self) -> Dict[str, Any]:
        return {
            "running": self.running,
            "mt5_state": self.mt5_state,
            "server_state": self.server_state,
            "mt5_error_code": self.mt5_error_code,
            "server_error_code": self.server_error_code,
            "resolved_symbol": self.resolved_symbol,
            "last_error": self.last_error,
            "last_sync_at": self.last_sync_at,
            "sent": self.sent,
            "acknowledged": self.acknowledged,
            "deduplicated": self.deduplicated,
            "queued": self.queued,
            "failed": self.failed,
            "last_candle_per_timeframe": dict(self.last_candle_per_timeframe),
            "acked_candle_per_timeframe": dict(self.acked_candle_per_timeframe),
        }


def _backoff_delay(attempt: int) -> float:
    """Bounded exponential backoff with jitter (attempt 0 -> ~2..4s)."""
    base = min(2.0 ** (attempt + 1), 60.0)
    return base * (0.5 + random.random() * 0.5)


class SyncEngine:
    def __init__(self, config, collector: Optional[ClosedCandleCollector] = None,
                 ledger: Optional[DeliveryLedger] = None,
                 client: Optional[ServerClient] = None,
                 on_status: Optional[Callable[[Dict[str, Any]], None]] = None,
                 status_refresh_seconds: float = 1.0):
        self._config = config
        self._collector = collector or ClosedCandleCollector(
            config.symbol,
            mt5_path=getattr(config, "mt5_path", ""),
            mt5_timeout_ms=getattr(config, "mt5_timeout_ms", 15000),
        )
        self._ledger = ledger or DeliveryLedger(config.resolved_ledger_path())
        self._client = client or ServerClient(config.backend_url, config.sender_token,
                                              config.sender_id,
                                              config.request_timeout_seconds)
        self._on_status = on_status
        self._status_refresh = status_refresh_seconds
        self._stop_event = threading.Event()
        self._paused = threading.Event()
        self._status = EngineStatus()
        self._status_mutex = threading.Lock()
        self._thread: Optional[threading.Thread] = None
        self._collected_count = 1
        self._reconciled = False

    # -- lifecycle ----------------------------------------------------------

    def start(self) -> None:
        self._stop_event.clear()
        self._paused.clear()
        self._thread = threading.Thread(target=self._run_loop, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=10)

    def pause(self) -> None:
        self._paused.set()

    def resume(self) -> None:
        self._paused.clear()

    def status_snapshot(self) -> Dict[str, Any]:
        with self._status_mutex:
            return self._status.snapshot()

    # -- status updates -----------------------------------------------------

    def _update_status(self, **kwargs: Any) -> None:
        with self._status_mutex:
            for key, value in kwargs.items():
                setattr(self._status, key, value)
            snapshot = self._status.snapshot()
        if self._on_status is not None:
            try:
                self._on_status(snapshot)
            except Exception:
                pass

    # -- main loop ----------------------------------------------------------

    def _run_loop(self) -> None:
        attempt = 0
        self._update_status(running=True, mt5_state="STARTING",
                            server_state="STARTING")
        while not self._stop_event.is_set():
            if self._paused.is_set():
                self._update_status(last_error="paused")
                self._stop_event.wait(self._status_refresh)
                continue

            try:
                self._sync_once()
                attempt = 0
                # A completed loop proves the server accepted our credential and
                # answered. Without this the Server field never left STARTING,
                # which read as a permanent hang even when sync was healthy.
                self._update_status(server_state="ONLINE", server_error_code="",
                                    last_error="")
                self._stop_event.wait(self._config.poll_interval_seconds)
            except ProtocolError as exc:
                attempt += 1
                if exc.code == "SENDER_AUTH_REJECTED" or "authorization" in exc.message.lower():
                    self._update_status(server_state="AUTH_FAILED",
                                        server_error_code=exc.code,
                                        last_error=f"server rejected credential: {exc.message}")
                else:
                    self._update_status(server_state="ERROR",
                                        server_error_code=exc.code,
                                        last_error=f"server: {exc.message}")
                self._stop_event.wait(_backoff_delay(min(attempt, 8)))
            except Exception as exc:  # pragma: no cover - defensive
                attempt += 1
                self._update_status(server_state="ERROR",
                                    server_error_code="UNEXPECTED",
                                    last_error=f"unexpected: {exc}")
                self._stop_event.wait(_backoff_delay(min(attempt, 8)))

    def _set_mt5_state(self, result: Dict[str, Any]) -> None:
        if result["ok"]:
            self._update_status(mt5_state="READY", mt5_error_code="",
                                resolved_symbol=self._collector.resolved_symbol)
        else:
            code = str(result.get("error_code") or "")
            self._update_status(mt5_state="UNAVAILABLE", mt5_error_code=code,
                                last_error=self._mt5_hint(code, result.get("message", "")))

    @staticmethod
    def _mt5_hint(code: str, message: str) -> str:
        """Turn a stable MT5 error code into actionable guidance.

        The GUI shows the reason a terminal read failed. Without this the user
        only saw "UNAVAILABLE" with no clue whether the package, the terminal,
        the login, or the symbol was at fault.
        """
        if code == "MT5_CLIENT_MISSING":
            return ("sender is missing its MT5 bridge module; reinstall the app "
                    f"({message})")
        if code == "MT5_TERMINAL_UNAVAILABLE":
            return ("MetaTrader5 is not reachable. Run the MT5 diagnostic button "
                    "for the exact error code. Usual causes: the terminal is not "
                    "running or not logged in; Algo Trading is off; ASTRA Sender "
                    "and terminal64.exe run at DIFFERENT privilege levels (elevate "
                    "both or neither); or a suffixed broker terminal needs its "
                    "path set in Settings. "
                    f"({message})")
        if code == "MT5_SYMBOL_UNRESOLVED":
            return ("no XAUUSD/GOLD symbol is visible in MT5. Add the gold symbol "
                    f"to Market Watch in the terminal. ({message})")
        if code in ("MARKET_DATA_MISSING", "MARKET_DATA_INVALID"):
            return f"MT5 returned no usable closed candle: {message}"
        return f"MT5 unavailable ({code or 'unknown'}): {message}"

    def _sync_once(self) -> None:
        result = self._ensure_collector()
        if not result["ok"]:
            self._set_mt5_state(result)
            # MT5 being down must not hide the server state. Probe the backend
            # so the user can tell "server unreachable" from "local MT5 down".
            self._probe_server()
            return
        self._update_status(mt5_state="READY", mt5_error_code="",
                            resolved_symbol=self._collector.resolved_symbol)

        if not self._reconciled:
            self._reconcile_from_server()

        pending_all = self._ledger.pending_identities()
        if pending_all:
            self._update_status(queued=len(pending_all))

        now = int(time.time())
        anything_sent = False
        for timeframe in self._config.timeframes:
            if self._stop_event.is_set():
                return
            self._sync_timeframe(timeframe, now)
            if self._stop_event.is_set():
                return
            anything_sent = True

        self._update_status(last_sync_at=int(time.time()))
        self._update_status(queued=len(self._ledger.pending_identities()))
        # Every send in this pass succeeded, so the credential is accepted and
        # the server answered. Record that here rather than only in the loop, so
        # the state is correct no matter which entry point drove the sync.
        self._update_status(server_state="ONLINE", server_error_code="")

    def _probe_server(self) -> None:
        """Authenticated reachability probe; never raises into the loop."""
        try:
            self._client.ping()
        except ProtocolError as exc:
            if exc.code == "SENDER_AUTH_REJECTED" or "authorization" in exc.message.lower():
                self._update_status(server_state="AUTH_FAILED",
                                    server_error_code=exc.code)
            else:
                self._update_status(server_state="ERROR",
                                    server_error_code=exc.code)
        except Exception as exc:  # pragma: no cover - defensive
            self._update_status(server_state="ERROR",
                                server_error_code="UNEXPECTED")
            self._update_status(last_error=f"server probe failed: {exc}")
        else:
            self._update_status(server_state="ONLINE", server_error_code="")

    # -- reconciliation -----------------------------------------------------

    def _reconcile_from_server(self) -> None:
        """Align the local ledger with the server's authoritative state once.

        The query is scoped to this sender's own source identity. The unscoped
        aggregate would mix in other senders' candles and could mark a candle
        acknowledged that this sender never had persisted, so it is never used
        here. A failure is non-fatal: the normal sync loop simply re-sends.
        """
        source = self._config.sender_id
        try:
            response = self._client.sync_latest(source)
        except ProtocolError as exc:
            self._update_status(last_error=f"reconcile skipped: {exc.message}")
            return
        self._reconciled = True

        entries = response.get("data")
        if not isinstance(entries, list):
            return
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            symbol = str(entry.get("symbol") or "")
            timeframe = str(entry.get("timeframe") or "")
            open_time = entry.get("last_open_time_utc")
            if not symbol or not timeframe or open_time is None:
                continue
            # The server stated it holds this exact candle for our source; that
            # is an explicit acknowledgment, so record it as such.
            self._ledger.record_acknowledged(
                source, symbol, timeframe, int(open_time),
                self._ledger.make_identity(source, symbol, timeframe,
                                           int(open_time)),
                int(entry.get("count") or 0))

    # -- backfill planning --------------------------------------------------

    def plan_backfill(self, source_id: str, symbol: str, timeframe: str,
                      open_times: List[int], max_bars: int,
                      ledger: Optional[DeliveryLedger] = None) -> List[int]:
        """Plan a bounded, chronological, idempotent catch-up/backfill.

        Returns the most recent unacknowledged open times, oldest first, capped
        at ``max_bars``. Acknowledged candles are never re-planned, so a
        backfill cannot duplicate the normal live sync. ``max_bars <= 0`` plans
        nothing. Identity is (source, symbol, timeframe, open time) — never a
        price — so a re-run of the same window is a no-op server-side.
        """
        if max_bars <= 0 or not open_times:
            return []
        book = ledger if ledger is not None else self._ledger
        ordered = sorted(int(t) for t in open_times)
        unacked = [t for t in ordered
                   if not book.is_acknowledged(source_id, symbol, timeframe, t)]
        return unacked[-max_bars:]

    # -- collector ----------------------------------------------------------

    def _ensure_collector(self) -> Dict[str, Any]:
        # Re-initialize on transient terminal failures.
        if self._collector_state_ok():
            return {"ok": True}
        startup = self._collector.start()
        if startup.ok:
            self._collector_ready = True
            return {"ok": True}
        self._collector_ready = False
        return {"ok": False, "error_code": startup.error_code,
                "message": startup.message}

    def _collector_state_ok(self) -> bool:
        return getattr(self, "_collector_ready", False)

    # -- one timeframe ------------------------------------------------------

    def _sync_timeframe(self, timeframe: str, now: int) -> None:
        source = self._config.sender_id
        symbol = self._collector.resolved_symbol or self._config.symbol

        # 1. Latest closed candle.
        latest = self._collector.read_latest_closed(timeframe)
        if not latest["ok"]:
            returned = latest.get("message", "")
            if "no closed candle" not in returned.lower():
                self._update_status(last_error=f"{timeframe}: {returned}")
            return
        candle = latest["candle"]
        self._update_status(last_candle_per_timeframe={
            timeframe: candle.to_payload()})

        ack_time = self._ledger.latest_acknowledged(source, symbol, timeframe)
        last_acked_candle = None
        if ack_time is not None:
            last_acked_candle = {"timeframe": timeframe, "open_time": ack_time}
        if last_acked_candle is not None:
            with self._status_mutex:
                self._status.acked_candle_per_timeframe[timeframe] = last_acked_candle

        # 2. Already acknowledged -> skip (dedup).
        if ack_time is not None and candle.time == ack_time:
            with self._status_mutex:
                self._status.deduplicated += 1
            return

        # 3. Catch-up: when the server ack trails the latest closed candle by
        #    more than one bar, fetch a bounded window of closed bars and send
        #    them chronologically.
        candle_windows = [candle]
        if ack_time is not None and candle.time > ack_time:
            gap = candle.time - ack_time
            if gap > 1800:   # more than one 15-minute-ish interval
                recent = self._collector.read_recent_closed(
                    timeframe, self._config.catchup_bar_limit)
                if recent["ok"]:
                    bounded = [c for c in recent["candles"]
                               if c.time > (ack_time or 0)
                               and c.time <= candle.time][:self._config.catchup_max_bars]
                    if bounded:
                        candle_windows = bounded

        # 4. Filter to unacked open times, oldest first.
        candidates = [c for c in candle_windows]
        if len(candidates) > 1:
            candidates.sort(key=lambda c: c.time)
        unacked_payloads: List[Dict[str, Any]] = []
        for c in candidates:
            if self._ledger.is_acknowledged(source, symbol, timeframe, c.time):
                continue
            unacked_payloads.append(c.to_payload())
        if not unacked_payloads:
            return

        # 5. Send (bounded batch size to keep requests small).
        max_batch = 16
        for i in range(0, len(unacked_payloads), max_batch):
            batch = unacked_payloads[i:i + max_batch]
            batch_identity = self._ledger.make_identity(
                source, symbol, timeframe, int(batch[0]["time"]))
            for c in batch:
                self._ledger.record_pending(source, symbol, timeframe,
                                            int(c["time"]),
                                            self._ledger.make_identity(
                                                source, symbol, timeframe,
                                                int(c["time"])))
            with self._status_mutex:
                self._status.queued = len(self._ledger.pending_identities())
            self._send_batch(source, symbol, timeframe, batch, batch_identity)

    def _send_batch(self, source: str, symbol: str, timeframe: str,
                    batch: List[Dict[str, Any]], identity: str) -> None:
        try:
            response = self._client.ingest_candles(source, symbol, timeframe, batch)
        except ProtocolError as exc:
            # Uncertain delivery: keep pending, do not mark failed.
            with self._status_mutex:
                self._status.failed = self._ledger.counts().get("failed", 0)
            self._update_status(server_state="ERROR",
                                last_error=f"{timeframe}: {exc.message}")
            raise
        except Exception as exc:  # pragma: no cover
            self._update_status(server_state="ERROR",
                                last_error=f"{timeframe}: {exc}")
            raise

        accepted = response.get("data", {}).get("accepted", [])
        duplicates = response.get("data", {}).get("duplicates", [])
        rejected = response.get("data", {}).get("rejected", [])

        with self._status_mutex:
            self._status.sent += len(batch)
        for entry in accepted:
            state = entry.get("state", "")
            sent_candle = self._find_open_time(batch, entry)
            if sent_candle is None:
                continue
            if state == "stored_new":
                self._ledger.record_acknowledged(
                    source, symbol, timeframe, sent_candle["time"],
                    self._ledger.make_identity(source, symbol, timeframe,
                                               sent_candle["time"]),
                    int(response.get("data", {}).get("received", 0)))
                with self._status_mutex:
                    self._status.acknowledged += 1
                    self._status.acked_candle_per_timeframe[timeframe] = {
                        "timeframe": timeframe, "open_time": int(sent_candle["time"])}
            elif state == "already_stored":
                self._ledger.record_acknowledged(
                    source, symbol, timeframe, sent_candle["time"],
                    self._ledger.make_identity(source, symbol, timeframe,
                                               sent_candle["time"]),
                    int(response.get("data", {}).get("received", 0)))
                with self._status_mutex:
                    self._status.deduplicated += 1
        for entry in rejected:
            detail = entry.get("reason", "")
            sent_candle = self._find_open_time(batch, entry)
            if sent_candle is not None:
                self._ledger.record_failed(
                    source, symbol, timeframe, sent_candle["time"], detail,
                    self._ledger.make_identity(source, symbol, timeframe,
                                               sent_candle["time"]))
                self._update_status(last_error=f"{timeframe}: rejected: {detail}")
        # duplicates entries (already stored server-side) also count as acked.
        for entry in duplicates:
            sent_candle = self._find_open_time(batch, entry)
            if sent_candle is not None:
                self._ledger.record_acknowledged(
                    source, symbol, timeframe, sent_candle["time"],
                    self._ledger.make_identity(source, symbol, timeframe,
                                               sent_candle["time"]),
                    int(response.get("data", {}).get("received", 0)))
                with self._status_mutex:
                    self._status.deduplicated += 1
        self._update_status()

    @staticmethod
    def _find_open_time(batch: List[Dict[str, Any]], entry: Any) -> Optional[Dict[str, Any]]:
        """Match an ack entry back to its candle. The server ack body embeds
        open_time in the identity string; we reconstruct it from the local
        batch by timeframe+time to stay schema-version tolerant."""
        identity = entry.get("identity", "") if isinstance(entry, dict) else ""
        for candidate in batch:
            if f"open_time={int(candidate['time'])}" in identity:
                return candidate
            if candidate.get("time") == entry.get("time"):
                return candidate
        return None