"""Sync engine: the loop that keeps the receiver caught up.

One pass does, in order:

  1. Read the newest closed candle per timeframe from MT5 and store it locally.
     Storing is idempotent, so a re-read never duplicates a row.
  2. Ask the receiver what it holds for each stream (authoritative). If the
     receiver cannot be reached, fall back to the local acknowledgment ledger.
  3. Plan the difference: candles we hold and the receiver has not confirmed,
     plus any local gaps the source must fill.
  4. Fill local gaps from MT5 when the broker still has the bars, and record
     gaps the broker no longer serves so they are reported, never hidden.
  5. Deliver the plan as one chronological stream, batching by candle count.
  6. Record a candle as acknowledged only when the receiver explicitly named
     its unique id and stated it persisted it.

A first run against a destination with no history primes a bounded window so
the downstream engine has a decision-grade feature window immediately, instead
of waiting dozens of live candles.
"""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional, Sequence

from . import planner as planning
from .config import CollectorConfig
from .identity import (
    CandleKey,
    IRREGULAR_TIMEFRAMES,
    TIMEFRAMES,
    timeframe_rank,
)
from .ledger import DeliveryLedger
from .mt5_reader import (
    MT5Unavailable,
    Mt5Reader,
    drop_forming_candle,
)
from .receiver import IngestOutcome, ReceiverClient, ReceiverError
from .store import CandleStore, StoredCandle, validate_candle


@dataclass
class CollectorStatus:
    """Everything the UI shows. Zeros and Nones are real, never placeholders."""

    running: bool = False
    paused: bool = False
    mt5_state: str = "STARTING"       # STARTING | READY | ERROR
    receiver_state: str = "STARTING"  # STARTING | ONLINE | AUTH_FAILED | ERROR
    resolved_symbol: str = ""
    broker: str = ""
    clock_offset_seconds: int = 0
    destination: str = ""
    last_sync_at: int = 0
    last_error: str = ""
    last_error_code: str = ""

    last_read_per_timeframe: Dict[str, Dict[str, Any]] = field(default_factory=dict)
    last_acked_per_timeframe: Dict[str, Dict[str, Any]] = field(default_factory=dict)
    stored_count_per_timeframe: Dict[str, int] = field(default_factory=dict)
    gap_count_per_timeframe: Dict[str, int] = field(default_factory=dict)

    sent_total: int = 0
    acknowledged_total: int = 0
    deduplicated_total: int = 0
    queued_total: int = 0
    failed_total: int = 0
    rejected_total: int = 0

    local_gaps: List[str] = field(default_factory=list)
    notes: List[str] = field(default_factory=list)

    def snapshot(self) -> Dict[str, Any]:
        return {
            "running": self.running,
            "paused": self.paused,
            "mt5_state": self.mt5_state,
            "receiver_state": self.receiver_state,
            "resolved_symbol": self.resolved_symbol,
            "broker": self.broker,
            "clock_offset_seconds": self.clock_offset_seconds,
            "destination": self.destination,
            "last_sync_at": self.last_sync_at,
            "last_error": self.last_error,
            "last_error_code": self.last_error_code,
            "last_read_per_timeframe": dict(self.last_read_per_timeframe),
            "last_acked_per_timeframe": dict(self.last_acked_per_timeframe),
            "stored_count_per_timeframe": dict(self.stored_count_per_timeframe),
            "gap_count_per_timeframe": dict(self.gap_count_per_timeframe),
            "sent_total": self.sent_total,
            "acknowledged_total": self.acknowledged_total,
            "deduplicated_total": self.deduplicated_total,
            "queued_total": self.queued_total,
            "failed_total": self.failed_total,
            "rejected_total": self.rejected_total,
            "local_gaps": list(self.local_gaps),
            "notes": list(self.notes),
        }


# Backoff bounds for retrying an unreachable receiver or terminal.
BACKOFF_MIN_SECONDS = 5
BACKOFF_MAX_SECONDS = 300
# Candles per ingest request. Small enough to stay well inside a request-size
# limit, large enough that a 300-bar prime is a few requests, not 300.
BATCH_SIZE = 50


class SyncEngine:
    def __init__(self, config: CollectorConfig, store: CandleStore,
                 ledger: DeliveryLedger, client: ReceiverClient,
                 reader: Optional[Mt5Reader] = None,
                 log: Optional[Callable[[str], None]] = None):
        self._config = config
        self._store = store
        self._ledger = ledger
        self._client = client
        self._reader = reader
        self._log = log or (lambda message: None)

        self._status = CollectorStatus()
        self._status.destination = ledger.destination_id(config.backend_url)
        self._status_lock = threading.Lock()

        self._stop_event = threading.Event()
        self._pause_event = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._backoff = BACKOFF_MIN_SECONDS
        self._remote_cache: Dict[tuple, List[int]] = {}
        self._remote_cache_at: float = 0.0
        self._primed_destinations: set = set()

    # -- lifecycle ----------------------------------------------------------

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._run, name="astra-sync",
                                        daemon=True)
        self._thread.start()

    def stop(self, timeout: float = 10.0) -> None:
        self._stop_event.set()
        thread = self._thread
        if thread and thread.is_alive():
            thread.join(timeout=timeout)
        if self._reader is not None:
            self._reader.disconnect()
        with self._status_lock:
            self._status.running = False

    def pause(self) -> None:
        self._pause_event.set()
        with self._status_lock:
            self._status.paused = True

    def resume(self) -> None:
        self._pause_event.clear()
        with self._status_lock:
            self._status.paused = False

    @property
    def paused(self) -> bool:
        return self._pause_event.is_set()

    def status(self) -> Dict[str, Any]:
        with self._status_lock:
            return self._status.snapshot()

    # -- loop ---------------------------------------------------------------

    def _run(self) -> None:
        with self._status_lock:
            self._status.running = True
        while not self._stop_event.is_set():
            if self._pause_event.is_set():
                self._stop_event.wait(1.0)
                continue
            started = time.time()
            try:
                self.sync_once()
                self._backoff = BACKOFF_MIN_SECONDS
            except ReceiverError as exc:
                self._set_error(exc.code, exc.message)
                self._sleep_backoff()
                continue
            except MT5Unavailable as exc:
                self._set_error(exc.code, exc.message)
                self._sleep_backoff()
                continue
            except Exception as exc:  # defensive: never kill the loop
                self._set_error("UNEXPECTED", f"{type(exc).__name__}: {exc}")
                self._sleep_backoff()
                continue
            elapsed = time.time() - started
            wait = max(1.0, self._config.poll_interval_seconds - elapsed)
            self._stop_event.wait(wait)
        with self._status_lock:
            self._status.running = False

    def _sleep_backoff(self) -> None:
        self._stop_event.wait(self._backoff)
        self._backoff = min(BACKOFF_MAX_SECONDS, max(BACKOFF_MIN_SECONDS,
                                                     self._backoff * 2))

    # -- one pass -----------------------------------------------------------

    def sync_once(self) -> planning.DeliveryPlan:
        """Run one full pass. Returns the plan that was executed."""
        self._ensure_reader()

        # 1. Read and store the newest closed candle per timeframe. A fault on
        #    one timeframe (e.g. a thin stream with no history yet) must not
        #    stop the other eight from syncing, so each is isolated.
        #    The newest stored open time is read *before* capturing, so the
        #    W1/MN1 repair below can still see the window that capturing is
        #    about to close over.
        previous_newest: Dict[str, Optional[int]] = {
            tf: self._store.latest_open_time(self._symbol(), tf)
            for tf in self._config.timeframes
        }
        newest_closed: Dict[str, int] = {}
        for timeframe in self._config.timeframes:
            try:
                latest = self._capture_timeframe(timeframe)
                if latest is not None:
                    newest_closed[timeframe] = latest
            except MT5Unavailable as exc:
                self._set_error(exc.code, f"{timeframe}: {exc.message}")

        # 2. Ask the receiver what it holds; fall back to the local ledger.
        remote = self._receiver_open_times()
        acknowledged = self._acknowledged_open_times()
        self._reconcile(remote)
        self._probe_receiver()

        # 3. Plan.
        local = self._local_open_times()
        plan = planning.plan_delivery(
            destination=self._status.destination,
            local_open_times=local,
            remote_open_times=remote,
            acknowledged=acknowledged)

        # 4. First contact with this destination: prime a bounded window.
        self._apply_prime(plan, local, remote, acknowledged)

        # 5. Fill local gaps from MT5 where the broker still has the bars.
        self._fill_gaps(plan)
        # 5b. W1/MN1 have no fixed bar length, so they are excluded from
        #     arithmetic gap detection above. Repair them against the source's
        #     own bar grid instead, or a weekly bar missed during downtime would
        #     be lost silently.
        self._repair_irregular_streams(plan, newest_closed, previous_newest)

        # 6. Deliver, in order.
        self._deliver(plan)

        with self._status_lock:
            self._status.last_sync_at = int(time.time())
            self._status.notes = list(plan.notes)[:20]
            self._status.local_gaps = [
                note for note in plan.notes if "local gap" in note][:20]
        return plan

    # -- steps --------------------------------------------------------------

    def _ensure_reader(self) -> None:
        if self._reader is None:
            self._reader = Mt5Reader(symbol=self._config.symbol)
        try:
            if self._reader.resolved_symbol:
                return
            self._reader.connect()
        except MT5Unavailable:
            # A dead terminal must not mask a healthy receiver: report MT5 as
            # the failing side and continue so the receiver can still be probed.
            self._reader = Mt5Reader(symbol=self._config.symbol)
            raise
        with self._status_lock:
            self._status.mt5_state = "READY"
            self._status.resolved_symbol = self._reader.resolved_symbol
            self._status.broker = self._reader.broker
            self._status.clock_offset_seconds = self._reader.clock_offset_seconds()

    def _capture_timeframe(self, timeframe: str) -> Optional[int]:
        assert self._reader is not None
        now = int(time.time())
        offset = self._reader.clock_offset_seconds()
        try:
            candles = self._reader.read_closed(timeframe, 2)
        except MT5Unavailable as exc:
            self._set_error(exc.code, f"{timeframe}: {exc.message}")
            return None
        candles = drop_forming_candle(candles, timeframe, now, offset)
        if not candles:
            return None
        newest = candles[-1]
        reason = validate_candle(
            newest.symbol, timeframe, newest.open_time_utc, newest.open,
            newest.high, newest.low, newest.close, now, offset)
        if reason:
            self._set_error("MARKET_DATA_INVALID", f"{timeframe}: {reason}")
            return None
        stored = StoredCandle(
            uid=CandleStore.make_uid(newest.symbol, timeframe,
                                     newest.open_time_utc),
            symbol=newest.symbol, timeframe=timeframe,
            open_time_utc=newest.open_time_utc, open=newest.open,
            high=newest.high, low=newest.low, close=newest.close,
            tick_volume=newest.tick_volume, spread=newest.spread,
            real_volume=newest.real_volume, source=newest.broker,
            stored_at=now)
        self._store.put(stored)
        with self._status_lock:
            self._status.last_read_per_timeframe[timeframe] = {
                "open_time_utc": newest.open_time_utc,
                "open": newest.open, "high": newest.high,
                "low": newest.low, "close": newest.close,
                "uid": stored.uid,
            }
            self._status.stored_count_per_timeframe[timeframe] = \
                self._store.count(newest.symbol, timeframe)
        return newest.open_time_utc

    def _receiver_open_times(self) -> Dict[tuple, List[int]]:
        """What the receiver holds, per stream. Empty on failure (the local
        ledger then acts as the fallback), never invented."""
        result: Dict[tuple, List[int]] = {}
        for timeframe in self._config.timeframes:
            symbol = self._symbol()
            try:
                result[(symbol, timeframe)] = \
                    self._client.remote_open_times(symbol, timeframe)
            except ReceiverError:
                return {}
        return result

    def _acknowledged_open_times(self) -> Dict[tuple, List[int]]:
        out: Dict[tuple, List[int]] = {}
        destination = self._status.destination
        for timeframe in self._config.timeframes:
            out[(self._symbol(), timeframe)] = \
                self._ledger.acknowledged_open_times(
                    destination, self._symbol(), timeframe)
        return out

    def _local_open_times(self) -> Dict[tuple, List[int]]:
        """Local open times per stream, capped to the newest ``max_plan_bars``.

        The cap is what bounds a first run: only the newest window of a stream
        is ever a delivery candidate, so capturing history locally cannot turn
        into uploading months of it. Older candles stay local.
        """
        out: Dict[tuple, List[int]] = {}
        for timeframe in self._config.timeframes:
            times = self._store.open_times(self._symbol(), timeframe)
            limit = self._config.max_plan_bars
            if limit > 0 and len(times) > limit:
                times = times[-limit:]
            out[(self._symbol(), timeframe)] = times
        return out

    def _reconcile(self, remote: Dict[tuple, List[int]]) -> None:
        """Adopt the receiver's authoritative state for candles we hold.

        When the receiver reports it already has a candle we hold locally, that
        is proof of persistence — the strongest confirmation available. This
        closes the lost-response case: a request that committed but whose reply
        was lost leaves the candle pending forever otherwise, because nothing
        ever re-reports it.

        Scoped to (symbol, timeframe) streams we actually hold, so a receiver
        holding other senders' candles cannot mark our candles acknowledged.
        """
        if not remote:
            return
        destination = self._status.destination
        for timeframe in self._config.timeframes:
            symbol = self._symbol()
            held = remote.get((symbol, timeframe))
            if not held:
                continue
            local_times = set(self._store.open_times(symbol, timeframe))
            for open_time in held:
                if open_time not in local_times:
                    continue
                key = CandleKey(symbol, timeframe, int(open_time))
                if not self._ledger.is_acknowledged(destination, key):
                    self._ledger.record_acknowledged(
                        destination, key, detail="reconciled from receiver")

    def _apply_prime(self, plan: planning.DeliveryPlan,
                     local: Dict[tuple, List[int]],
                     remote: Dict[tuple, List[int]],
                     acknowledged: Dict[tuple, List[int]]) -> None:
        """On first contact with a destination, seed a bounded history window.

        Only when the receiver holds nothing for the stream and we have never
        confirmed anything to it. An established destination is left alone:
        priming there would re-send bars it already has.

        The history is read from the terminal and stored locally before it is
        planned, because the store only holds what has been captured so far —
        planning from it alone would prime one bar and call that a window.
        """
        destination = self._status.destination
        if destination in self._primed_destinations:
            return
        if self._config.prime_history_bars <= 0:
            self._primed_destinations.add(destination)
            return
        assert self._reader is not None
        now = int(time.time())
        offset = self._reader.clock_offset_seconds()
        primed_any = False

        for stream in plan.streams:
            key = (stream.symbol, stream.timeframe)
            if remote.get(key) or acknowledged.get(key):
                continue
            if stream.timeframe not in TIMEFRAMES:
                continue
            try:
                # read_closed starts at index 1, so every bar it returns is
                # already closed. Reading exactly N gives the newest N closed
                # bars with no boundary bar left over to deliver out of order.
                raw = self._reader.read_closed(
                    stream.timeframe, self._config.prime_history_bars)
            except MT5Unavailable as exc:
                plan.notes.append(
                    f"{stream.symbol} {stream.timeframe}: cannot prime history "
                    f"({exc.message})")
                continue
            raw = drop_forming_candle(raw, stream.timeframe, now, offset)
            to_store: List[StoredCandle] = []
            for candle in raw:
                reason = validate_candle(
                    candle.symbol, stream.timeframe, candle.open_time_utc,
                    candle.open, candle.high, candle.low, candle.close, now,
                    offset)
                if reason:
                    continue
                to_store.append(StoredCandle(
                    uid=CandleStore.make_uid(candle.symbol, stream.timeframe,
                                             candle.open_time_utc),
                    symbol=candle.symbol, timeframe=stream.timeframe,
                    open_time_utc=candle.open_time_utc, open=candle.open,
                    high=candle.high, low=candle.low, close=candle.close,
                    tick_volume=candle.tick_volume, spread=candle.spread,
                    real_volume=candle.real_volume, source=candle.broker,
                    stored_at=now))
            if not to_store:
                continue
            self._store.put_many(to_store)
            # Prime exactly the newest N closed bars, contiguously. The extra
            # bar read above only confirms the boundary is closed; it is not
            # part of the window, because delivering an isolated newer bar
            # before the window would break the chronological stream.
            window = sorted(c.open_time_utc for c in to_store)
            if len(window) > self._config.prime_history_bars:
                window = window[-self._config.prime_history_bars:]
            stream.to_send = window
            stream.gaps = []
            primed_any = True

        if primed_any:
            # The prime replaces the whole plan: it is the complete, contiguous
            # opening window, so nothing else needs sending in this pass.
            plan.queue = []
            plan.gaps_to_fill = []
            for stream in plan.streams:
                for open_time in stream.to_send:
                    plan.queue.append(
                        CandleKey(stream.symbol, stream.timeframe, open_time))
            plan.queue.sort(key=lambda k: k.sort_key())
            plan.notes.append(
                f"first contact with {destination}: primed up to "
                f"{self._config.prime_history_bars} closed candles per stream")
        self._primed_destinations.add(destination)

    def _fill_gaps(self, plan: planning.DeliveryPlan) -> None:
        """Fetch locally missing bars from MT5 when the broker still has them.

        A gap the broker no longer serves is left in the plan notes as an
        explicit, reported gap. It is never silently papered over.
        """
        assert self._reader is not None
        unresolved: List[str] = []
        for symbol, timeframe, gap_start, gap_end, missing in list(plan.gaps_to_fill):
            if timeframe not in TIMEFRAMES:
                continue
            step = _step_seconds(timeframe)
            if step is None:
                continue
            wanted = (gap_end - gap_start) // step + 1
            newest = self._store.latest_open_time(symbol, timeframe)
            if newest is None:
                continue
            # Reach back past gap_start, not merely across the width of the gap,
            # because read_closed returns the newest N bars. Bounded by
            # catchup_max_bars so a very long outage cannot turn into one
            # unbounded read, and walked in catchup_bar_limit pages so memory
            # stays flat: each page is stored and released before the next.
            reach = (newest - gap_start) // step + wanted + 5
            reach = min(reach, self._config.catchup_max_bars)
            if reach <= 0:
                continue
            chunk = max(1, self._config.catchup_bar_limit)
            now = int(time.time())
            offset = self._reader.clock_offset_seconds()
            destination = self._status.destination
            filled = 0
            # Walk backwards from the newest bar, so each page is older than
            # the last. Within a page the bars are oldest-first, which is the
            # order the store and the queue want.
            offset_bars = 0
            while offset_bars < reach:
                want = min(chunk, reach - offset_bars)
                try:
                    page = self._reader.read_closed(timeframe, want,
                                                    skip=offset_bars)
                except MT5Unavailable as exc:
                    unresolved.append(
                        f"{symbol} {timeframe}: cannot fill gap from "
                        f"{gap_start} to {gap_end} ({exc.message})")
                    break
                offset_bars += want
                if not page:
                    break
                to_store: List[StoredCandle] = []
                for candle in page:
                    if (candle.open_time_utc < gap_start
                            or candle.open_time_utc > gap_end):
                        continue
                    reason = validate_candle(
                        candle.symbol, timeframe, candle.open_time_utc,
                        candle.open, candle.high, candle.low, candle.close,
                        now, offset)
                    if reason:
                        continue
                    to_store.append(StoredCandle(
                        uid=CandleStore.make_uid(symbol, timeframe,
                                                 candle.open_time_utc),
                        symbol=symbol, timeframe=timeframe,
                        open_time_utc=candle.open_time_utc, open=candle.open,
                        high=candle.high, low=candle.low, close=candle.close,
                        tick_volume=candle.tick_volume, spread=candle.spread,
                        real_volume=candle.real_volume, source=candle.broker,
                        stored_at=now))
                    filled += 1
                if not to_store:
                    continue
                self._store.put_many(to_store)
                for stored in to_store:
                    # Only queue what the receiver has not confirmed. A gap-fill
                    # re-reads a window that may overlap already-delivered
                    # candles, and re-queueing those would break the
                    # never-send-twice guarantee.
                    if self._ledger.is_acknowledged(destination, stored.key):
                        continue
                    plan.queue.append(stored.key)
            if filled < missing:
                unresolved.append(
                    f"{symbol} {timeframe}: broker supplied {filled} of "
                    f"{missing} missing candle(s) between {gap_start} and "
                    f"{gap_end}; the rest are unavailable")
        if unresolved:
            plan.notes.extend(unresolved)
            with self._status_lock:
                self._status.local_gaps = unresolved[:20]
        if plan.queue:
            plan.queue.sort(key=lambda k: k.sort_key())

    def _repair_irregular_streams(
            self, plan: planning.DeliveryPlan,
            newest_closed: Dict[str, int],
            previous_newest: Dict[str, Optional[int]]) -> None:
        """Re-read W1/MN1 from the source's own grid, so a missed bar is not lost.

        Arithmetic gap detection is skipped for these two timeframes because a
        weekly or monthly bar has no fixed length, so a missing one cannot be
        told apart from a market holiday by arithmetic alone. But skipping the
        *repair* would lose the bar silently, which is worse. Instead the
        source is asked which bars exist in the window since the last stored
        bar, and any bar it reports that is not stored locally is persisted.

        ``previous_newest`` is the newest stored open time from *before* this
        pass captured a bar, which is what makes the missed window visible:
        capturing stores the newest bar first, so reading the watermark
        afterwards would show no window at all.

        Only open times the source itself reports are ever used, so no bar is
        invented and a holiday simply does not appear.
        """
        assert self._reader is not None
        added: List[str] = []
        for symbol, timeframe in sorted(
                {(s.symbol, s.timeframe) for s in plan.streams},
                key=lambda k: (k[0], timeframe_rank(k[1]))):
            if timeframe not in IRREGULAR_TIMEFRAMES:
                continue
            newest = newest_closed.get(timeframe)
            if newest is None:
                continue
            previous = previous_newest.get(timeframe)
            if previous is None or newest <= previous:
                continue
            try:
                window = self._reader.read_closed(timeframe, 400)
            except MT5Unavailable as exc:
                plan.notes.append(
                    f"{symbol} {timeframe}: cannot check for missed bars "
                    f"({exc.message})")
                continue
            now = int(time.time())
            offset = self._reader.clock_offset_seconds()
            destination = self._status.destination
            to_store: List[StoredCandle] = []
            for candle in window:
                if not previous < candle.open_time_utc <= newest:
                    continue
                reason = validate_candle(
                    candle.symbol, timeframe, candle.open_time_utc,
                    candle.open, candle.high, candle.low, candle.close,
                    now, offset)
                if reason:
                    continue
                to_store.append(StoredCandle(
                    uid=CandleStore.make_uid(symbol, timeframe,
                                             candle.open_time_utc),
                    symbol=symbol, timeframe=timeframe,
                    open_time_utc=candle.open_time_utc, open=candle.open,
                    high=candle.high, low=candle.low, close=candle.close,
                    tick_volume=candle.tick_volume, spread=candle.spread,
                    real_volume=candle.real_volume, source=candle.broker,
                    stored_at=now))
            if not to_store:
                continue
            self._store.put_many(to_store)
            for stored in to_store:
                added.append(stored.uid)
                if self._ledger.is_acknowledged(destination, stored.key):
                    continue
                plan.queue.append(stored.key)
        if added:
            plan.queue.sort(key=lambda k: k.sort_key())
            plan.notes.append(
                f"recovered {len(added)} weekly/monthly candle(s) from the "
                f"source grid")

    def _deliver(self, plan: planning.DeliveryPlan) -> None:
        """Send the plan, in order, in bounded batches.

        Each candle is marked pending before its request goes out and is only
        acknowledged when the receiver names its unique id. A candle the
        response does not account for stays pending, so a lost response is
        retried with the same id rather than being mistaken for success.
        """
        if not plan.queue:
            return
        destination = self._status.destination
        with self._status_lock:
            self._status.queued_total = len(plan.queue)

        for key in plan.queue:
            self._ledger.record_pending(destination, key)
        with self._status_lock:
            self._status.queued_total = len(
                self._ledger.pending(destination))

        batches = _group_batches(plan.queue)
        for symbol, timeframe, keys in batches:
            candles: List[StoredCandle] = []
            for key in keys:
                candle = self._store.get(key.uid)
                if candle is None:
                    continue
                candles.append(candle)
            if not candles:
                continue
            for key in keys:
                self._ledger.record_attempt(destination, key)
            try:
                outcome = self._client.ingest(
                    symbol, timeframe, candles,
                    source_id=self._config.sender_id,
                    clock_offset_seconds=self._reader.clock_offset_seconds())
            except ReceiverError as exc:
                # Uncertain delivery: keep every candle pending and surface the
                # failure. Nothing is marked acknowledged.
                with self._status_lock:
                    self._status.failed_total += len(candles)
                    self._status.receiver_state = (
                        "AUTH_FAILED" if exc.code == "AUTH_REJECTED" else "ERROR")
                    self._status.last_error = f"{timeframe}: {exc.message}"
                    self._status.last_error_code = exc.code
                raise
            self._apply_outcome(outcome, keys, timeframe)

    def _apply_outcome(self, outcome: IngestOutcome,
                       keys: Sequence[CandleKey], timeframe: str) -> None:
        destination = self._status.destination
        by_uid = {key.uid: key for key in keys}
        newly = 0
        duplicates = 0
        for uid in outcome.confirmed:
            key = by_uid.get(uid)
            if key is None:
                continue
            already = self._ledger.is_acknowledged(destination, key)
            self._ledger.record_acknowledged(destination, key,
                                             detail="receiver confirmed")
            if already:
                duplicates += 1
            else:
                newly += 1
            with self._status_lock:
                self._status.last_acked_per_timeframe[timeframe] = {
                    "open_time_utc": key.open_time_utc, "uid": uid}
        for uid, reason in outcome.rejected.items():
            key = by_uid.get(uid)
            if key is None:
                continue
            self._ledger.record_rejected(destination, key, detail=reason)
            with self._status_lock:
                self._status.rejected_total += 1
        for uid in outcome.unconfirmed:
            key = by_uid.get(uid)
            if key is None:
                continue
            # Stays pending. Reported honestly rather than assumed stored.
            with self._status_lock:
                self._status.failed_total += 1
        with self._status_lock:
            self._status.sent_total += len(keys)
            self._status.acknowledged_total += newly
            self._status.deduplicated_total += duplicates
            self._status.queued_total = len(self._ledger.pending(destination))
            counts = self._ledger.counts(destination)
            self._status.rejected_total = counts.get("rejected",
                                                     self._status.rejected_total)
            self._status.receiver_state = "ONLINE"
            self._status.last_error = ""
            self._status.last_error_code = ""

    def _probe_receiver(self) -> None:
        """Authenticated liveness probe. Never raises into the loop."""
        try:
            self._client.health()
        except ReceiverError as exc:
            with self._status_lock:
                self._status.receiver_state = (
                    "AUTH_FAILED" if exc.code == "AUTH_REJECTED" else "ERROR")
                self._status.last_error_code = exc.code
                if exc.code != "AUTH_REJECTED":
                    self._status.last_error = exc.message
        except Exception as exc:  # defensive
            with self._status_lock:
                self._status.receiver_state = "ERROR"
                self._status.last_error_code = "UNEXPECTED"
                self._status.last_error = f"receiver probe failed: {exc}"
        else:
            with self._status_lock:
                if self._status.receiver_state != "ONLINE":
                    self._status.receiver_state = "ONLINE"
                self._status.last_error_code = ""

    # -- helpers ------------------------------------------------------------

    def _symbol(self) -> str:
        if self._reader is not None and self._reader.resolved_symbol:
            return self._reader.resolved_symbol
        return self._config.symbol

    def _set_error(self, code: str, message: str) -> None:
        with self._status_lock:
            self._status.last_error = message
            self._status.last_error_code = code
            if code.startswith("MT5"):
                self._status.mt5_state = "ERROR"

    def refresh_ledger_counters(self) -> None:
        """Re-read persisted counters so a restart shows real numbers."""
        destination = self._status.destination
        counts = self._ledger.counts(destination)
        with self._status_lock:
            self._status.acknowledged_total = counts.get("acknowledged", 0)
            self._status.rejected_total = counts.get("rejected", 0)
            self._status.queued_total = counts.get("pending", 0)


def _step_seconds(timeframe: str) -> Optional[int]:
    from .identity import TIMEFRAME_SECONDS, IRREGULAR_TIMEFRAMES
    if timeframe in IRREGULAR_TIMEFRAMES:
        return None
    return TIMEFRAME_SECONDS.get(timeframe)


def _group_batches(queue: Sequence[CandleKey]):
    """Split the ordered queue into (symbol, timeframe, keys) batches.

    Order is preserved globally: batches are emitted in the order their first
    candle appears, so the receiver still sees a single chronological stream.
    A stream change forces a new batch, which keeps one request to one stream.
    """
    batches: List[tuple] = []
    current: List[CandleKey] = []
    current_stream = None
    for key in queue:
        stream = (key.symbol, key.timeframe)
        if current and (stream != current_stream or len(current) >= BATCH_SIZE):
            batches.append((current_stream[0], current_stream[1], current))
            current = []
        if not current:
            current_stream = stream
        current.append(key)
    if current and current_stream:
        batches.append((current_stream[0], current_stream[1], current))
    return batches
