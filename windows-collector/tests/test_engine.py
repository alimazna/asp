"""Engine tests: the end-to-end delivery guarantees.

Each test drives the real ``SyncEngine`` against the real store, ledger,
planner, and receiver client. The only doubles are the MT5 terminal and the
receiving endpoint (``tests/doubles.py``), both external boundaries. All candle
values come from the deterministic test feed — none of this is production data.
"""

from __future__ import annotations

import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from collector.config import CollectorConfig  # noqa: E402
from collector.engine import SyncEngine  # noqa: E402
from collector.identity import (  # noqa: E402
    CandleKey,
    TIMEFRAMES,
    TIMEFRAME_SECONDS,
)
from collector.ledger import DeliveryLedger  # noqa: E402
from collector.receiver import ReceiverClient, ReceiverError  # noqa: E402
from collector.store import CandleStore  # noqa: E402

from doubles import FakeMt5Terminal, FakeReader, FakeReceiver  # noqa: E402

M15_STEP = 900


def aligned(now: int, step: int = M15_STEP) -> int:
    return now - now % step


class EngineHarness:
    """Wires a real engine to the doubles. Test scaffolding only."""

    def __init__(self, timeframes=None, total_bars=300, broker_offset=0,
                 symbol="XAUUSD", token="test-token"):
        self.directory = tempfile.mkdtemp(prefix="astra_engine_")
        self.terminal = FakeMt5Terminal(
            symbol=symbol, total_bars=total_bars,
            last_time=aligned(int(time.time()), M15_STEP),
            clock_offset_seconds=broker_offset)
        self.receiver = FakeReceiver(token=token)
        self.reader = FakeReader(self.terminal, symbol=symbol)
        self.config = CollectorConfig(
            backend_url=self.receiver.url,
            sender_id="collector-test",
            symbol=symbol,
            timeframes=list(timeframes or TIMEFRAMES),
            prime_history_bars=0,          # explicit per test
            catchup_max_bars=100000,
            catchup_bar_limit=20000,
            poll_interval_seconds=30,
            request_timeout_seconds=10,
            allow_insecure_http=True)      # the double listens on plain HTTP
        self.store = CandleStore(os.path.join(self.directory, "candles.sqlite"))
        self.ledger = DeliveryLedger(os.path.join(self.directory, "delivery.sqlite"))
        self.client = ReceiverClient(self.receiver.url, token,
                                     timeout_seconds=10)
        self.engine = SyncEngine(self.config, self.store, self.ledger,
                                 self.client, reader=self.reader)

    def reopen(self):
        """Simulate a restart: fresh store/ledger handles, same files."""
        self.store.close()
        self.ledger.close()
        self.store = CandleStore(os.path.join(self.directory, "candles.sqlite"))
        self.ledger = DeliveryLedger(os.path.join(self.directory, "delivery.sqlite"))
        self.client = ReceiverClient(self.receiver.url, "test-token",
                                     timeout_seconds=10)
        self.engine = SyncEngine(self.config, self.store, self.ledger,
                                 self.client, reader=self.reader)

    def sent_uids(self):
        uids = []
        for request in self.receiver.state.requests:
            for candle in request.get("candles", []):
                uids.append(candle.get("uid"))
        return uids

    def sent_times(self):
        times = []
        for request in self.receiver.state.requests:
            for candle in request.get("candles", []):
                times.append(int(candle.get("open_time_utc", 0)))
        return times

    def close(self):
        self.receiver.stop()
        try:
            self.store.close()
        except Exception:
            pass
        try:
            self.ledger.close()
        except Exception:
            pass


class EngineDeliveryTest(unittest.TestCase):
    def setUp(self):
        self.h = EngineHarness(timeframes=["M15", "H1"])
        self.addCleanup(self.h.close)

    def test_first_run_delivers_latest_closed_candle_for_each_timeframe(self):
        plan = self.h.engine.sync_once()
        self.assertGreaterEqual(plan.send_count, 2)
        for timeframe in ("M15", "H1"):
            key = (self.h.terminal.symbol, timeframe)
            self.assertTrue(
                self.h.ledger.acknowledged_open_times(
                    self.h.config.destination_id(), key[0], key[1]),
                f"{timeframe} should have an acknowledged candle")
        self.assertEqual(self.h.engine.status()["receiver_state"], "ONLINE")

    def test_forming_candle_is_never_delivered(self):
        """The newest bar at index 0 is still forming and must never be sent."""
        self.h.engine.sync_once()
        forming_time = self.h.terminal.last_time  # index 0 = forming
        self.assertNotIn(forming_time, self.h.sent_times(),
                         "the forming candle must never be submitted")

    def test_second_run_resends_nothing(self):
        self.h.engine.sync_once()
        self.h.receiver.state.requests = []
        self.h.engine.sync_once()
        self.assertEqual(self.h.sent_uids(), [],
                         "an acknowledged candle must not be re-sent")

    def test_restart_does_not_resend_acknowledged_candles(self):
        self.h.engine.sync_once()
        self.h.receiver.state.requests = []
        self.h.reopen()
        self.h.engine.sync_once()
        self.assertEqual(self.h.sent_uids(), [],
                         "local state must survive a restart")

    def test_duplicate_ingest_creates_no_second_record(self):
        """Server-side idempotency: the same uid twice stores once."""
        self.h.engine.sync_once()
        stored_after_first = len(self.h.receiver.state.stored)
        # Force the same candle through again, as a lost-response retry would.
        self.h.ledger.clear_destination(self.h.config.destination_id())
        self.h.engine.sync_once()
        self.assertEqual(len(self.h.receiver.state.stored), stored_after_first,
                         "a repeated uid must not create a second record")

    def test_lost_response_is_retried_and_not_marked_acked(self):
        """Commit-then-lose-the-response: the candle stays pending, then lands.

        Scoped to a single stream so the duplicate assertion is unambiguous.
        """
        h = EngineHarness(timeframes=["M15"])
        self.addCleanup(h.close)
        h.receiver.state.commit_then_fail = True
        with self.assertRaises(ReceiverError):
            h.engine.sync_once()
        destination = h.config.destination_id()
        pending = h.ledger.pending(destination)
        self.assertTrue(pending, "an unconfirmed delivery must stay pending")
        for entry in pending:
            self.assertFalse(
                h.ledger.is_acknowledged(destination, entry.key),
                "an unconfirmed candle must never be marked acknowledged")
        stored_after_commit = len(h.receiver.state.stored)
        self.assertEqual(stored_after_commit, 1,
                         "the receiver did commit before the response was lost")
        # Next pass retries with the same uid. The receiver already holds the
        # data, so it reports a duplicate and no second record is created.
        h.receiver.state.requests = []
        h.engine.sync_once()
        self.assertEqual(len(h.receiver.state.stored), 1,
                         "the retry must not create a duplicate record")
        for entry in pending:
            self.assertTrue(
                h.ledger.is_acknowledged(destination, entry.key),
                "the retried candle must now be acknowledged")

    def test_unconfirmed_candle_is_not_marked_acknowledged(self):
        """A response that does not name the candle proves nothing."""
        self.h.receiver.state.fail_next = 1
        with self.assertRaises(ReceiverError):
            self.h.engine.sync_once()
        destination = self.h.config.destination_id()
        for entry in self.h.ledger.pending(destination):
            self.assertFalse(self.h.ledger.is_acknowledged(destination, entry.key))

    def test_auth_failure_is_reported_and_delivers_nothing(self):
        self.h.client = ReceiverClient(self.h.receiver.url, "wrong-token",
                                       timeout_seconds=10)
        self.h.engine = SyncEngine(self.h.config, self.h.store, self.h.ledger,
                                   self.h.client, reader=self.h.reader)
        with self.assertRaises(ReceiverError):
            self.h.engine.sync_once()
        self.assertEqual(self.h.receiver.state.stored, {},
                         "nothing may be stored without a valid credential")
        self.assertEqual(self.h.engine.status()["receiver_state"],
                         "AUTH_FAILED")

    def test_delivery_is_chronological(self):
        self.h.config.prime_history_bars = 20
        self.h.engine.sync_once()
        times = self.h.sent_times()
        self.assertGreater(len(times), 5)
        self.assertEqual(times, sorted(times),
                         "delivery must be one chronological stream")

    def test_prime_seeds_a_decision_grade_window_on_first_contact(self):
        self.h.config.prime_history_bars = 60
        self.h.engine.sync_once()
        per_stream = {}
        for request in self.h.receiver.state.requests:
            key = (request["symbol"], request["timeframe"])
            per_stream[key] = per_stream.get(key, 0) + len(request["candles"])
        for key, count in per_stream.items():
            self.assertGreaterEqual(
                count, 55,
                f"{key} should be primed above the 55-bar minimum, got {count}")

    def test_prime_does_not_repeat_on_second_run(self):
        self.h.config.prime_history_bars = 60
        self.h.engine.sync_once()
        self.h.receiver.state.requests = []
        self.h.engine.sync_once()
        self.assertEqual(self.h.sent_uids(), [],
                         "the prime must not be re-sent once acknowledged")


class DestinationSwitchTest(unittest.TestCase):
    """The failure this design exists to prevent.

    Changing the receiving endpoint must not make candles look delivered to a
    receiver that has never seen them.
    """

    def setUp(self):
        self.h = EngineHarness(timeframes=["M15"])
        self.addCleanup(self.h.close)

    def test_switching_destination_resends_everything(self):
        self.h.engine.sync_once()
        self.assertGreater(len(self.h.receiver.state.stored), 0)
        first_destination = self.h.config.destination_id()

        # A second, independent receiver.
        second = FakeReceiver(token="test-token")
        self.addCleanup(second.stop)
        self.h.config.backend_url = second.url
        self.h.client = ReceiverClient(second.url, "test-token", timeout_seconds=10)
        self.h.engine = SyncEngine(self.h.config, self.h.store, self.h.ledger,
                                   self.h.client, reader=self.h.reader)
        self.assertNotEqual(first_destination, self.h.config.destination_id(),
                            "a different URL must yield a different identity")
        self.h.engine.sync_once()
        self.assertGreater(len(second.state.stored), 0,
                           "the new receiver must receive the candles it lacks")

    def test_local_acknowledgments_do_not_leak_across_destinations(self):
        key = CandleKey("XAUUSD", "M15", aligned(int(time.time()), M15_STEP))
        self.h.ledger.record_acknowledged("receiver-a", key)
        self.assertTrue(self.h.ledger.is_acknowledged("receiver-a", key))
        self.assertFalse(self.h.ledger.is_acknowledged("receiver-b", key))


class GapAndRecoveryTest(unittest.TestCase):
    def setUp(self):
        self.h = EngineHarness(timeframes=["M15"], total_bars=300)
        self.addCleanup(self.h.close)

    def test_local_gap_is_filled_from_the_source(self):
        """A bar we never captured is fetched and stored before delivery."""
        base = aligned(self.h.terminal.last_time, M15_STEP)
        # Pretend we already hold the ends but not the middle.
        from collector.store import StoredCandle
        for i in (5, 6, 8, 9):
            open_time = base - i * M15_STEP
            self.h.store.put(StoredCandle(
                uid=CandleStore.make_uid("XAUUSD", "M15", open_time),
                symbol="XAUUSD", timeframe="M15", open_time_utc=open_time,
                open=2000.0, high=2001.0, low=1999.0, close=2000.5))
        self.h.config.prime_history_bars = 0
        self.h.engine.sync_once()
        # The missing bar at index 7 must now be present locally.
        missing_time = base - 7 * M15_STEP
        self.assertTrue(self.h.store.has(
            CandleStore.make_uid("XAUUSD", "M15", missing_time)),
            "a gap the broker still serves must be filled")

    def test_mt5_down_reports_error_without_claiming_success(self):
        self.h.terminal.set_available(False)
        with self.assertRaises(Exception):
            self.h.engine.sync_once()
        self.assertEqual(self.h.receiver.state.stored, {},
                         "no data may be delivered while the terminal is down")

    def test_status_reports_real_counters(self):
        self.h.config.prime_history_bars = 10
        self.h.engine.sync_once()
        status = self.h.engine.status()
        self.assertGreater(status["acknowledged_total"], 0)
        self.assertEqual(status["rejected_total"], 0)
        self.assertEqual(status["mt5_state"], "READY")
        self.assertEqual(status["receiver_state"], "ONLINE")
        self.assertTrue(status["destination"])

    def test_status_after_restart_reflects_persisted_state(self):
        self.h.config.prime_history_bars = 10
        self.h.engine.sync_once()
        self.h.reopen()
        self.h.engine.refresh_ledger_counters()
        self.assertGreater(self.h.engine.status()["acknowledged_total"], 0)


class BrokerClockTest(unittest.TestCase):
    """A UTC+3 broker must not have its intraday bars rejected as future-dated."""

    def test_intraday_bars_are_accepted_with_broker_offset(self):
        h = EngineHarness(timeframes=["M1", "M15"], broker_offset=3 * 3600)
        self.addCleanup(h.close)
        h.engine.sync_once()
        for timeframe in ("M1", "M15"):
            acked = h.ledger.acknowledged_open_times(
                h.config.destination_id(), "XAUUSD", timeframe)
            self.assertTrue(acked, f"{timeframe} must be accepted, not dropped")


class ValidationTest(unittest.TestCase):
    def test_config_rejects_plain_http_without_optin(self):
        config = CollectorConfig(backend_url="http://example.com",
                                 sender_id="x")
        self.assertTrue(any("plain HTTP" in p for p in config.validate()))

    def test_config_accepts_https(self):
        config = CollectorConfig(backend_url="https://example.com",
                                 sender_id="x")
        self.assertEqual(config.validate(), [])

    def test_config_rejects_bad_timeframe(self):
        config = CollectorConfig(backend_url="https://example.com",
                                 sender_id="x", timeframes=["M15", "H2"])
        self.assertTrue(any("Unknown timeframe" in p for p in config.validate()))


class LifecycleTest(unittest.TestCase):
    """The engine's own state machine, as the GUI drives it."""

    def setUp(self):
        self.h = EngineHarness(timeframes=["M15"])
        self.addCleanup(self.h.close)

    def test_paused_is_a_property_not_a_call(self):
        engine = self.h.engine
        self.assertFalse(engine.paused)
        engine.pause()
        self.assertTrue(engine.paused)
        engine.resume()
        self.assertFalse(engine.paused)

    def test_status_reflects_pause_and_resume(self):
        self.h.engine.pause()
        self.assertTrue(self.h.engine.status()["paused"])
        self.h.engine.resume()
        self.assertFalse(self.h.engine.status()["paused"])

    def test_status_exposes_the_destination_identity(self):
        status = self.h.engine.status()
        self.assertEqual(status["destination"],
                         self.h.config.destination_id())
        self.assertTrue(status["destination"])

    def test_start_stop_is_idempotent(self):
        self.h.engine.start()
        self.h.engine.start()   # a second start must not spawn a second loop
        self.assertTrue(self.h.engine.status()["running"])
        self.h.engine.stop()
        self.assertFalse(self.h.engine.status()["running"])
        self.h.engine.stop()    # stopping twice must not raise

    def test_paused_loop_does_not_deliver(self):
        self.h.engine.pause()
        self.h.engine.start()
        try:
            time.sleep(1.5)
        finally:
            self.h.engine.stop()
        self.assertEqual(self.h.receiver.state.stored, {},
                         "a paused engine must send nothing")


class NineTimeframeTest(unittest.TestCase):
    """Every supported timeframe, end to end, in one pass.

    The manual diagnostic covered this once; this makes it a permanent check so
    a regression in any single timeframe's mapping is caught by the suite.
    """

    def setUp(self):
        self.h = EngineHarness(total_bars=320)
        self.h.config.prime_history_bars = 300
        self.addCleanup(self.h.close)

    def test_all_nine_are_primed_acknowledged_and_contiguous(self):
        self.h.engine.sync_once()
        destination = self.h.config.destination_id()
        for timeframe in TIMEFRAMES:
            with self.subTest(timeframe=timeframe):
                stored = sorted(
                    int(c["open_time_utc"])
                    for c in self.h.receiver.state.stored.values()
                    if c["timeframe"] == timeframe)
                acked = self.h.ledger.acknowledged_open_times(
                    destination, "XAUUSD", timeframe)
                self.assertTrue(stored, f"{timeframe}: nothing stored")
                self.assertEqual(acked, stored,
                                 f"{timeframe}: acknowledged != stored")
                step = TIMEFRAME_SECONDS[timeframe]
                for earlier, later in zip(stored, stored[1:]):
                    self.assertEqual(later - earlier, step,
                                     f"{timeframe}: gap {earlier}->{later}")

    def test_no_forming_candle_on_any_timeframe(self):
        self.h.engine.sync_once()
        for timeframe in TIMEFRAMES:
            with self.subTest(timeframe=timeframe):
                step = TIMEFRAME_SECONDS[timeframe]
                anchor = self.h.terminal.last_time
                newest_closed = anchor - (anchor % step) - step
                stored = [int(c["open_time_utc"])
                          for c in self.h.receiver.state.stored.values()
                          if c["timeframe"] == timeframe]
                self.assertTrue(stored)
                self.assertLessEqual(max(stored), newest_closed)

    def test_second_pass_resends_nothing_on_any_timeframe(self):
        self.h.engine.sync_once()
        self.h.receiver.state.requests = []
        self.h.engine.sync_once()
        self.assertEqual(self.h.sent_uids(), [])


class OutageCatchUpTest(unittest.TestCase):
    """A long shutdown must not silently lose M1 candles.

    The original limits repaired only about three days of M1 before the read
    cap truncated the gap, and the loss was invisible: the plan reported
    success while thousands of candles were never delivered.
    """

    def _outage(self, down_minutes, config_tweak=None, timeframe="M1"):
        step = TIMEFRAME_SECONDS[timeframe]
        bars_down = int(down_minutes * 60 / step)
        directory = tempfile.mkdtemp(prefix="astra_outage_")
        terminal = FakeMt5Terminal(
            symbol="XAUUSD",
            total_bars=300 + bars_down + 80,
            last_time=int(time.time()))
        receiver = FakeReceiver(token="t")
        config = CollectorConfig(
            backend_url=receiver.url, sender_id="s", symbol="XAUUSD",
            timeframes=[timeframe], prime_history_bars=300,
            allow_insecure_http=True)
        if config_tweak:
            config_tweak(config)
        store = CandleStore(os.path.join(directory, "c.sqlite"))
        ledger = DeliveryLedger(os.path.join(directory, "d.sqlite"))
        client = ReceiverClient(receiver.url, "t", timeout_seconds=10)

        SyncEngine(config, store, ledger, client,
                   reader=FakeReader(terminal)).sync_once()
        before = max(int(c["open_time_utc"])
                     for c in receiver.state.stored.values())

        # A restart: the wall clock and the broker clock both move forward and
        # the reader is rebuilt, so the broker offset is measured afresh.
        shift = down_minutes * 60
        terminal.last_time += shift
        terminal.clock_offset_seconds += shift
        receiver.state.requests = []
        SyncEngine(config, store, ledger, client,
                   reader=FakeReader(terminal)).sync_once()

        delivered = {int(c["open_time_utc"])
                     for c in receiver.state.stored.values()}
        newest_closed = (terminal.last_time
                         - (terminal.last_time % step) - step)
        expected = list(range(before + step, newest_closed + step, step))
        missing = [t for t in expected if t not in delivered]
        store.close(); ledger.close(); receiver.stop()
        return expected, missing

    def test_a_week_of_downtime_loses_no_m1_candle(self):
        expected, missing = self._outage(7 * 24 * 60)
        self.assertEqual(missing, [],
                         f"lost {len(missing)} of {len(expected)} candles")

    def test_a_month_of_downtime_loses_no_m1_candle(self):
        expected, missing = self._outage(30 * 24 * 60)
        self.assertEqual(missing, [],
                         f"lost {len(missing)} of {len(expected)} candles")

    def test_every_timeframe_survives_a_day_of_downtime(self):
        """Including W1 and MN1, which have no fixed bar length.

        These two are excluded from arithmetic gap detection, so without a
        source-grid repair a weekly bar missed during downtime was lost with no
        warning at all.
        """
        for timeframe in TIMEFRAMES:
            with self.subTest(timeframe=timeframe):
                expected, missing = self._outage(
                    24 * 60, timeframe=timeframe)
                self.assertEqual(
                    missing, [],
                    f"{timeframe}: lost {len(missing)} of {len(expected)}")

    def test_a_weekly_bar_missed_during_downtime_is_recovered(self):
        expected, missing = self._outage(30 * 24 * 60, timeframe="W1")
        self.assertTrue(expected, "the scenario must span at least one week")
        self.assertEqual(missing, [],
                         f"lost {len(missing)} of {len(expected)} weekly bars")

    def test_a_monthly_bar_missed_during_downtime_is_recovered(self):
        expected, missing = self._outage(400 * 24 * 60, timeframe="MN1")
        self.assertEqual(missing, [],
                         f"lost {len(missing)} of {len(expected)} monthly bars")

    def test_catch_up_is_chunked_not_one_unbounded_read(self):
        """The repair walks back in bounded pages."""
        reads = []

        directory = tempfile.mkdtemp(prefix="astra_chunk_")
        terminal = FakeMt5Terminal(symbol="XAUUSD", total_bars=300 + 3000 + 80,
                                   last_time=int(time.time()))
        receiver = FakeReceiver(token="t")
        config = CollectorConfig(
            backend_url=receiver.url, sender_id="s", symbol="XAUUSD",
            timeframes=["M1"], prime_history_bars=300,
            catchup_bar_limit=500, catchup_max_bars=100000,
            max_plan_bars=100000, allow_insecure_http=True)
        store = CandleStore(os.path.join(directory, "c.sqlite"))
        ledger = DeliveryLedger(os.path.join(directory, "d.sqlite"))
        client = ReceiverClient(receiver.url, "t", timeout_seconds=10)

        class CountingReader(FakeReader):
            def read_closed(self, timeframe, count, skip=0):
                reads.append((count, skip))
                return super().read_closed(timeframe, count, skip)

        SyncEngine(config, store, ledger, client,
                   reader=CountingReader(terminal)).sync_once()
        shift = 2000 * 60
        terminal.last_time += shift
        terminal.clock_offset_seconds += shift
        reads.clear()
        SyncEngine(config, store, ledger, client,
                   reader=CountingReader(terminal)).sync_once()

        catch_up_reads = [(c, s) for c, s in reads if c > 2]
        self.assertGreater(len(catch_up_reads), 1,
                           "a 2000-bar repair should take several bounded reads")
        for count, _ in catch_up_reads:
            self.assertLessEqual(count, 500,
                                 "no single read may exceed the chunk size")
        store.close(); ledger.close(); receiver.stop()

    def test_a_config_that_cannot_repair_a_gap_is_rejected(self):
        config = CollectorConfig(
            backend_url="https://example.invalid", sender_id="s",
            catchup_max_bars=100)
        problems = config.validate()
        self.assertTrue(
            any("catchup_max_bars" in p for p in problems),
            f"a tiny catch-up window must be refused, got {problems}")

    def test_a_pre_existing_small_config_is_widened_not_rejected(self):
        """A file written before the bounds were widened must still start.

        Refusing it would strand an existing installation on a stale config.
        """
        old = {
            "backend_url": "https://example.invalid", "sender_id": "s",
            "symbol": "XAUUSD", "timeframes": ["M1"],
            "prime_history_bars": 300, "catchup_max_bars": 500,
            "catchup_bar_limit": 5000, "max_plan_bars": 2000,
            "poll_interval_seconds": 30, "request_timeout_seconds": 30,
            "allow_insecure_http": False,
        }
        config = CollectorConfig.from_dict(old)
        self.assertGreaterEqual(config.catchup_max_bars, 1000)
        self.assertEqual(config.validate(), [])

    def test_loading_never_narrows_a_bound(self):
        generous = {
            "backend_url": "https://example.invalid", "sender_id": "s",
            "catchup_max_bars": 500000, "max_plan_bars": 500000,
        }
        config = CollectorConfig.from_dict(generous)
        self.assertEqual(config.catchup_max_bars, 500000)
        self.assertEqual(config.max_plan_bars, 500000)

    def test_a_small_plan_window_still_repairs_a_long_outage(self):
        """``max_plan_bars`` bounds the planner window, not outage repair.

        A hole is bounded by the candles on either side of it, and nothing is
        stored inside the hole, so those neighbours are adjacent in the stored
        list however small the planner window is.
        """
        expected, missing = self._outage(
            30 * 24 * 60,
            lambda c: setattr(c, "max_plan_bars", 2000))
        self.assertEqual(missing, [],
                         f"lost {len(missing)} of {len(expected)} candles")


if __name__ == "__main__":
    unittest.main()
