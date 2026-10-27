"""Tests: identity, ordering, gaps, store, ledger, planner.

These cover the guarantees the design rests on. Everything here runs against
real modules; the only doubles are the MT5 terminal and the receiver, both of
which are external boundaries.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from collector.identity import (  # noqa: E402
    CandleKey,
    TIMEFRAMES,
    candle_uid,
    expected_sequence,
    find_gaps,
    is_contiguous,
    is_known_timeframe,
    sort_keys,
    timeframe_rank,
)
from collector.ledger import (  # noqa: E402
    DeliveryLedger,
    STATE_ACKNOWLEDGED,
    STATE_PENDING,
    STATE_REJECTED,
)
from collector.planner import (  # noqa: E402
    plan_catchup,
    plan_delivery,
    plan_prime,
    summarise,
)
from collector.store import (  # noqa: E402
    CandleStore,
    StoredCandle,
    validate_candle,
)

M15_STEP = 900
H1_STEP = 3600


def m15(open_time: int) -> int:
    return open_time - open_time % M15_STEP


class IdentityTest(unittest.TestCase):
    def test_uid_is_deterministic(self):
        a = candle_uid("XAUUSD", "M15", 1700000000)
        b = candle_uid("XAUUSD", "M15", 1700000000)
        self.assertEqual(a, b)

    def test_uid_differs_per_dimension(self):
        base = candle_uid("XAUUSD", "M15", 1700000000)
        self.assertNotEqual(base, candle_uid("GOLD", "M15", 1700000000))
        self.assertNotEqual(base, candle_uid("XAUUSD", "M30", 1700000000))
        self.assertNotEqual(base, candle_uid("XAUUSD", "M15", 1700000900))

    def test_uid_never_derived_from_prices(self):
        """Two candles with identical OHLC must still get different ids."""
        first = CandleKey("XAUUSD", "M15", 1700000000)
        second = CandleKey("XAUUSD", "M15", 1700000900)
        self.assertNotEqual(first.uid, second.uid)

    def test_uid_rejects_unknown_timeframe(self):
        with self.assertRaises(ValueError):
            candle_uid("XAUUSD", "H2", 1700000000)

    def test_uid_rejects_empty_symbol(self):
        with self.assertRaises(ValueError):
            candle_uid("", "M15", 1700000000)

    def test_all_nine_timeframes_known_and_ranked(self):
        self.assertEqual(len(TIMEFRAMES), 9)
        for index, timeframe in enumerate(TIMEFRAMES):
            self.assertTrue(is_known_timeframe(timeframe))
            self.assertEqual(timeframe_rank(timeframe), index)

    def test_ordering_is_chronological_then_timeframe(self):
        keys = [
            CandleKey("XAUUSD", "H1", 3600),
            CandleKey("XAUUSD", "M15", 3600),
            CandleKey("XAUUSD", "M15", 1800),
        ]
        ordered = sort_keys(keys)
        self.assertEqual([k.sort_key() for k in ordered],
                         [(1800, 2), (3600, 2), (3600, 4)])


class GapTest(unittest.TestCase):
    def test_single_missing_candle_is_reported(self):
        present = [0, 900, 2700, 3600]
        gaps = find_gaps("M15", present, 0, 3600)
        self.assertEqual(gaps, [(1800, 1800, 1)])

    def test_contiguous_run_has_no_gaps(self):
        present = [0, 900, 1800, 2700]
        self.assertEqual(find_gaps("M15", present, 0, 2700), [])
        self.assertTrue(is_contiguous("M15", present))

    def test_run_of_missing_candles_is_one_range(self):
        present = [0, 900, 4500]
        gaps = find_gaps("M15", present, 0, 4500)
        self.assertEqual(gaps, [(1800, 3600, 3)])

    def test_irregular_timeframes_never_invent_gaps(self):
        """A missing weekly bar is indistinguishable from a holiday."""
        self.assertEqual(find_gaps("W1", [0, 604800 * 3], 0, 604800 * 3), [])
        self.assertEqual(find_gaps("MN1", [0, 2592000 * 2]), [])

    def test_expected_sequence_aligns_and_steps(self):
        seq = expected_sequence("XAUUSD", "M15", 0, 3)
        self.assertEqual(seq, [0, 900, 1800])

    def test_expected_sequence_rejects_unaligned_start(self):
        with self.assertRaises(ValueError):
            expected_sequence("XAUUSD", "M15", 100, 3)

    def test_expected_sequence_rejects_irregular_timeframe(self):
        with self.assertRaises(ValueError):
            expected_sequence("XAUUSD", "MN1", 0, 3)


class StoreTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.mkdtemp(prefix="astra_store_")
        self.store = CandleStore(os.path.join(self.directory, "candles.sqlite"))

    def tearDown(self):
        self.store.close()

    def _candle(self, timeframe: str, open_time: int,
                symbol: str = "XAUUSD") -> StoredCandle:
        return StoredCandle(
            uid=CandleStore.make_uid(symbol, timeframe, open_time),
            symbol=symbol, timeframe=timeframe, open_time_utc=open_time,
            open=2000.0, high=2001.0, low=1999.0, close=2000.5,
            tick_volume=10, spread=20, real_volume=0, source="TestBroker",
            stored_at=1)

    def test_single_file_holds_all_nine_timeframes(self):
        base = m15(1700000000)
        for timeframe in TIMEFRAMES:
            self.assertTrue(self.store.put(self._candle(timeframe, base)))
        self.assertEqual(self.store.count(), 9)
        streams = self.store.streams()
        self.assertEqual(len(streams), 9)
        self.assertEqual([s[1] for s in streams], TIMEFRAMES)

    def test_put_is_idempotent(self):
        candle = self._candle("M15", m15(1700000000))
        self.assertTrue(self.store.put(candle))
        self.assertFalse(self.store.put(candle), "duplicate write must be a no-op")
        self.assertEqual(self.store.count(), 1)

    def test_repeat_write_does_not_mutate_stored_row(self):
        open_time = m15(1700000000)
        self.store.put(self._candle("M15", open_time))
        changed = StoredCandle(
            uid=CandleStore.make_uid("XAUUSD", "M15", open_time),
            symbol="XAUUSD", timeframe="M15", open_time_utc=open_time,
            open=1.0, high=1.0, low=1.0, close=1.0)
        self.store.put(changed)
        stored = self.store.get(changed.uid)
        self.assertEqual(stored.open, 2000.0, "captured candle is immutable")

    def test_put_many_is_atomic_and_counts_new_rows(self):
        base = m15(1700000000)
        batch = [self._candle("M15", base + i * M15_STEP) for i in range(5)]
        self.assertEqual(self.store.put_many(batch), 5)
        self.assertEqual(self.store.put_many(batch), 0)

    def test_open_times_are_ascending(self):
        base = m15(1700000000)
        for i in (3, 1, 2):
            self.store.put(self._candle("M15", base + i * M15_STEP))
        self.assertEqual(self.store.open_times("XAUUSD", "M15"),
                         [base + M15_STEP, base + 2 * M15_STEP,
                          base + 3 * M15_STEP])

    def test_recent_returns_oldest_first(self):
        base = m15(1700000000)
        for i in range(5):
            self.store.put(self._candle("M15", base + i * M15_STEP))
        recent = self.store.recent("XAUUSD", "M15", limit=3)
        self.assertEqual([c.open_time_utc for c in recent],
                         [base + 2 * M15_STEP, base + 3 * M15_STEP,
                          base + 4 * M15_STEP])

    def test_store_survives_reopen(self):
        base = m15(1700000000)
        self.store.put(self._candle("H1", base))
        self.store.close()
        reopened = CandleStore(os.path.join(self.directory, "candles.sqlite"))
        try:
            self.assertEqual(reopened.count(), 1)
            self.assertEqual(reopened.latest_open_time("XAUUSD", "H1"), base)
        finally:
            reopened.close()

    def test_validate_rejects_inconsistent_ohlc(self):
        now = 1700000000
        self.assertIsNotNone(validate_candle(
            "XAUUSD", "M15", now, 10.0, 9.0, 11.0, 10.0, now))
        self.assertIsNotNone(validate_candle(
            "XAUUSD", "M15", now, 10.0, 12.0, 9.0, 13.0, now))

    def test_validate_rejects_future_candle_in_broker_frame(self):
        now = 1700000000
        # Broker is 3h ahead: a stamp 2h ahead of UTC is legitimately closed.
        self.assertIsNone(validate_candle(
            "XAUUSD", "M15", now + 7200, 10.0, 11.0, 9.0, 10.0, now, 3 * 3600))
        # A stamp a day ahead is not.
        self.assertIsNotNone(validate_candle(
            "XAUUSD", "M15", now + 86400, 10.0, 11.0, 9.0, 10.0, now, 3 * 3600))

    def test_validate_rejects_non_positive_prices(self):
        now = 1700000000
        self.assertIsNotNone(validate_candle(
            "XAUUSD", "M15", now, 0.0, 11.0, 9.0, 10.0, now))
        self.assertIsNotNone(validate_candle(
            "XAUUSD", "M15", now, 10.0, 11.0, 9.0, -1.0, now))


class LedgerTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.mkdtemp(prefix="astra_ledger_")
        self.path = os.path.join(self.directory, "deliveries.sqlite")
        self.ledger = DeliveryLedger(self.path)
        self.key = CandleKey("XAUUSD", "M15", m15(1700000000))

    def tearDown(self):
        self.ledger.close()

    def test_destination_id_normalises_url(self):
        self.assertEqual(
            DeliveryLedger.destination_id("https://Example.com/"),
            DeliveryLedger.destination_id("http://example.com"))
        self.assertNotEqual(
            DeliveryLedger.destination_id("https://a.example.com"),
            DeliveryLedger.destination_id("https://b.example.com"))

    def test_pending_then_acknowledged(self):
        self.ledger.record_pending("dest", self.key)
        self.assertEqual(self.ledger.state("dest", self.key), STATE_PENDING)
        self.assertFalse(self.ledger.is_acknowledged("dest", self.key))
        self.ledger.record_acknowledged("dest", self.key)
        self.assertTrue(self.ledger.is_acknowledged("dest", self.key))

    def test_pending_does_not_downgrade_acknowledged(self):
        self.ledger.record_acknowledged("dest", self.key)
        self.ledger.record_pending("dest", self.key)
        self.assertTrue(self.ledger.is_acknowledged("dest", self.key),
                        "a re-queue must not undo a confirmation")

    def test_state_is_scoped_per_destination(self):
        """The core protection: a new destination has seen nothing."""
        self.ledger.record_acknowledged("dest-a", self.key)
        self.assertTrue(self.ledger.is_acknowledged("dest-a", self.key))
        self.assertFalse(self.ledger.is_acknowledged("dest-b", self.key),
                         "a different destination must not inherit acks")

    def test_attempts_are_counted(self):
        self.ledger.record_pending("dest", self.key)
        self.ledger.record_attempt("dest", self.key)
        self.ledger.record_attempt("dest", self.key)
        entry = self.ledger.pending("dest")[0]
        self.assertEqual(entry.attempts, 2)

    def test_rejected_is_recorded_and_not_retried(self):
        self.ledger.record_rejected("dest", self.key, detail="bad ohlc")
        self.assertEqual(self.ledger.state("dest", self.key), STATE_REJECTED)
        self.assertEqual(self.ledger.pending("dest"), [])
        self.assertEqual(len(self.ledger.rejected("dest")), 1)

    def test_acknowledged_open_times_is_a_set_not_a_watermark(self):
        base = m15(1700000000)
        for i in (0, 1, 4):
            key = CandleKey("XAUUSD", "M15", base + i * M15_STEP)
            self.ledger.record_acknowledged("dest", key)
        times = self.ledger.acknowledged_open_times("dest", "XAUUSD", "M15")
        self.assertEqual(times, [base, base + M15_STEP, base + 4 * M15_STEP])
        self.assertNotIn(base + 2 * M15_STEP, times,
                         "the hole must be visible, not hidden by a watermark")

    def test_counts_and_persistence(self):
        base = m15(1700000000)
        for i in range(3):
            self.ledger.record_acknowledged(
                "dest", CandleKey("XAUUSD", "M15", base + i * M15_STEP))
        self.ledger.record_pending(
            "dest", CandleKey("XAUUSD", "M15", base + 3 * M15_STEP))
        counts = self.ledger.counts("dest")
        self.assertEqual(counts[STATE_ACKNOWLEDGED], 3)
        self.assertEqual(counts[STATE_PENDING], 1)
        self.ledger.close()
        reopened = DeliveryLedger(self.path)
        try:
            self.assertEqual(reopened.counts("dest")[STATE_ACKNOWLEDGED], 3)
        finally:
            reopened.close()


class PlannerTest(unittest.TestCase):
    def test_watermark_hole_is_detected_and_planned(self):
        """The exact failure a watermark cannot see."""
        base = m15(1700000000)
        local = {("XAUUSD", "M15"): [base + i * M15_STEP for i in range(9)]}
        # Receiver has 0,1,2,6,7,8 — a hole at 3,4,5. Its watermark is 8, so a
        # watermark-based sender would conclude it is fully caught up.
        remote = {("XAUUSD", "M15"):
                  [base, base + M15_STEP, base + 2 * M15_STEP,
                   base + 6 * M15_STEP, base + 7 * M15_STEP,
                   base + 8 * M15_STEP]}
        plan = plan_delivery("dest", local, remote)
        stream = plan.streams[0]
        # The hole is repaired: exactly the three absent candles are re-planned,
        # even though the receiver's watermark equals the newest local candle.
        self.assertEqual(stream.to_send,
                         [base + 3 * M15_STEP, base + 4 * M15_STEP,
                          base + 5 * M15_STEP])
        self.assertEqual(stream.already_have,
                         [base, base + M15_STEP, base + 2 * M15_STEP,
                          base + 6 * M15_STEP, base + 7 * M15_STEP,
                          base + 8 * M15_STEP])
        self.assertEqual(plan.send_count, 3)

    def test_local_hole_is_reported_as_a_gap_to_fill(self):
        """A bar we never captured is a local gap, reported for source backfill."""
        base = m15(1700000000)
        # We are missing 3,4,5 ourselves.
        local = {("XAUUSD", "M15"):
                 [base + i * M15_STEP for i in (0, 1, 2, 6, 7, 8)]}
        plan = plan_delivery("dest", local, {})
        stream = plan.streams[0]
        self.assertEqual(stream.gaps,
                         [(base + 3 * M15_STEP, base + 5 * M15_STEP, 3)])
        self.assertEqual(len(plan.gaps_to_fill), 1)
        self.assertTrue(any("local gap" in n for n in plan.notes))

    def test_nothing_planned_when_receiver_has_everything(self):
        base = m15(1700000000)
        times = [base + i * M15_STEP for i in range(4)]
        plan = plan_delivery("dest", {("XAUUSD", "M15"): times},
                             {("XAUUSD", "M15"): times})
        self.assertEqual(plan.send_count, 0)
        self.assertFalse(plan.has_work)

    def test_queue_is_one_chronological_stream_across_timeframes(self):
        base = m15(1700000000)
        local = {
            ("XAUUSD", "M15"): [base + i * M15_STEP for i in range(4)],
            ("XAUUSD", "H1"): [base, base + H1_STEP],
        }
        plan = plan_delivery("dest", local, {})
        keys = [(k.open_time_utc, k.timeframe) for k in plan.queue]
        self.assertEqual(keys, sorted(keys, key=lambda x: (x[0], timeframe_rank(x[1]))))
        self.assertEqual(keys[0], (base, "M15"), "finer timeframe first")

    def test_acknowledged_fallback_suppresses_resend_when_receiver_unreachable(self):
        base = m15(1700000000)
        local = {("XAUUSD", "M15"): [base + i * M15_STEP for i in range(3)]}
        acked = {("XAUUSD", "M15"): [base, base + M15_STEP]}
        plan = plan_delivery("dest", local, {}, acked)
        self.assertEqual(plan.send_count, 1)
        self.assertEqual(plan.queue[0].open_time_utc, base + 2 * M15_STEP)

    def test_receiver_only_candles_are_noted_not_sent(self):
        base = m15(1700000000)
        plan = plan_delivery(
            "dest", {("XAUUSD", "M15"): [base]},
            {("XAUUSD", "M15"): [base, base + M15_STEP]})
        self.assertEqual(plan.send_count, 0)
        self.assertTrue(any("we do not have locally" in n for n in plan.notes))

    def test_catchup_is_bounded_and_oldest_first(self):
        base = m15(1700000000)
        local = [base + i * M15_STEP for i in range(20)]
        planned = plan_catchup(local, [], [], max_bars=5)
        self.assertEqual(planned, [base + i * M15_STEP for i in range(15, 20)])

    def test_catchup_never_replans_acknowledged(self):
        base = m15(1700000000)
        local = [base + i * M15_STEP for i in range(4)]
        acked = [base, base + M15_STEP]
        planned = plan_catchup(local, [], acked, max_bars=10)
        self.assertEqual(planned, [base + 2 * M15_STEP, base + 3 * M15_STEP])

    def test_prime_takes_newest_bounded_window(self):
        base = m15(1700000000)
        local = [base + i * M15_STEP for i in range(100)]
        primed = plan_prime(local, 60)
        self.assertEqual(len(primed), 60)
        self.assertEqual(primed[-1], base + 99 * M15_STEP)
        self.assertEqual(primed, sorted(primed))

    def test_prime_respects_end_cap(self):
        base = m15(1700000000)
        local = [base + i * M15_STEP for i in range(10)]
        primed = plan_prime(local, 5, end_utc=base + 6 * M15_STEP)
        self.assertEqual(primed, [base + 2 * M15_STEP, base + 3 * M15_STEP,
                                  base + 4 * M15_STEP, base + 5 * M15_STEP,
                                  base + 6 * M15_STEP])

    def test_summarise_reports_real_numbers(self):
        base = m15(1700000000)
        plan = plan_delivery("dest", {("XAUUSD", "M15"): [base]}, {})
        summary = summarise(plan)
        self.assertEqual(summary["send_count"], 1)
        self.assertEqual(summary["destination"], "dest")


class ReceiverWireContractTest(unittest.TestCase):
    """The client must understand the bytes the real server actually sends.

    These pin the two formats that differ between the sender's own vocabulary
    and the deployed ASTRA server's:

      * ingest acks name a candle by ``identity`` (a symbol/timeframe/open_time
        string), not by the sender's ``uid``;
      * ``/candles`` answers with ``data.bars[]`` where the open time is under
        ``time``, not ``data.candles[]``/``data.open_times[]``.

    Both were once wrong, and the bug was invisible because the test double
    spoke the sender's dialect. These tests use the server's dialect directly.
    """

    def _client_and_server(self):
        from tests.doubles import FakeReceiver
        server = FakeReceiver(token="t")
        return server, server.state

    def test_ingest_ack_by_server_identity_is_understood(self):
        from collector.receiver import ReceiverClient
        from collector.store import StoredCandle
        server, state = self._client_and_server()
        try:
            client = ReceiverClient(server.url, token="t")
            candle = StoredCandle(
                uid="XAUUSD:M15:1792905300@abc", symbol="XAUUSD",
                timeframe="M15", open_time_utc=1792905300,
                open=1.0, high=2.0, low=0.5, close=1.5,
                tick_volume=1, spread=1, real_volume=0, source="B",
                stored_at=0)
            outcome = client.ingest("XAUUSD", "M15", [candle])
            self.assertEqual(outcome.confirmed, [candle.uid],
                             "an ack naming the candle by server identity must "
                             "still confirm the sender's uid")
            self.assertEqual(outcome.unconfirmed, [])
            self.assertTrue(outcome.ok)
        finally:
            server.stop()

    def test_reconciliation_reads_server_bars_format(self):
        from collector.receiver import ReceiverClient
        server, state = self._client_and_server()
        try:
            with state.lock:
                state.stored["u1"] = {
                    "uid": "u1", "symbol": "XAUUSD", "timeframe": "M15",
                    "open_time_utc": 1792905300, "open": 1.0, "high": 2.0,
                    "low": 0.5, "close": 1.5}
            client = ReceiverClient(server.url, token="t")
            self.assertEqual(client.remote_open_times("XAUUSD", "M15"),
                             [1792905300],
                             "reconciliation must read data.bars[].time")
        finally:
            server.stop()


if __name__ == "__main__":
    unittest.main()
