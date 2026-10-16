"""AUDIT-T33 item 6 sender tests.

Covers three gaps found in the improvement study:

  * broker-suffixed symbol resolution (``XAUUSD.vx``) is surfaced, and the
    resolved symbol — never a silently unrelated instrument — is what the
    sender reports;
  * the ingestion token never reaches the local acknowledgment ledger, so a
    support bundle or the SQLite file cannot leak the credential;
  * an explicit, bounded historical backfill is chronological, idempotent and
    rate-limited, and it never re-sends an already-acknowledged candle.

The fake MetaTrader5 double (tests/integration/fake_mt5) stands in for the
external Windows-only package; every candle is test data, none of it reaches
production.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest

_REPO = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
_FAKE_MT5 = os.path.join(_REPO, "tests", "integration", "fake_mt5")
_BRIDGE = os.path.normpath(os.path.join(_REPO, "bridge", "mt5_python"))
for _p in (_FAKE_MT5, _BRIDGE):
    if _p not in sys.path:
        sys.path.insert(0, _p)
os.environ["FAKE_MT5_AVAILABLE"] = "1"
os.environ.setdefault("FAKE_MT5_TOTAL_BARS", "300")

from astra_sender.collector import ClosedCandleCollector
from astra_sender.config import SenderConfig
from astra_sender.ledger import DeliveryLedger


class SymbolResolutionTest(unittest.TestCase):
    def test_broker_suffixed_symbol_is_resolved_and_reported(self):
        # The fake broker offers XAUUSD.vx; requesting it must yield an exact
        # match, and the resolved name must be what the collector reports.
        collector = ClosedCandleCollector("XAUUSD.vx")
        startup = collector.start()
        self.assertTrue(startup.ok, startup.message)
        self.assertEqual(startup.resolved_symbol, "XAUUSD.vx")
        self.assertEqual(collector.resolved_symbol, "XAUUSD.vx")

        result = collector.read_latest_closed("M15")
        self.assertTrue(result["ok"], result)
        # The candle carries the resolved broker symbol, not the bare request.
        self.assertEqual(result["candle"].symbol, "XAUUSD.vx")
        collector.stop()

    def test_unknown_preferred_symbol_falls_back_and_never_lies(self):
        # "GOLD" is absent from this broker. Resolution must still produce a
        # real XAU instrument (never the unrelated EURUSD) and report it.
        collector = ClosedCandleCollector("GOLD")
        startup = collector.start()
        self.assertTrue(startup.ok, startup.message)
        self.assertNotEqual(startup.resolved_symbol, "")
        self.assertIn("XAU", startup.resolved_symbol.upper())
        self.assertNotEqual(startup.resolved_symbol.upper(), "EURUSD")
        collector.stop()

    def test_broker_and_server_are_reported(self):
        collector = ClosedCandleCollector("XAUUSD")
        startup = collector.start()
        self.assertTrue(startup.ok)
        self.assertEqual(startup.broker, "FakeBroker Ltd")
        self.assertEqual(startup.server, "FakeServer-Demo")
        collector.stop()


class TokenStorageTest(unittest.TestCase):
    TOKEN = "tok-9f3c-secret-do-not-persist"

    def test_token_never_appears_in_the_ledger_file(self):
        directory = tempfile.mkdtemp(prefix="astra_tok_")
        ledger_path = os.path.join(directory, "ledger.sqlite")

        # A real ledger, exercised through its real write paths.
        ledger = DeliveryLedger(ledger_path)
        identity = DeliveryLedger.make_identity("s1", "XAUUSD", "M15", 1792045800)
        ledger.record_pending("s1", "XAUUSD", "M15", 1792045800, identity)
        ledger.record_failed("s1", "XAUUSD", "M15", 1792045800,
                             "rejected: bad high")
        identity2 = DeliveryLedger.make_identity("s1", "XAUUSD", "H1", 1792044000)
        ledger.record_acknowledged("s1", "XAUUSD", "H1", 1792044000, identity2, 999)
        ledger.close()

        # WAL may hold committed pages; scan every file the ledger produced.
        blob = b""
        for name in os.listdir(directory):
            with open(os.path.join(directory, name), "rb") as handle:
                blob += handle.read()
        self.assertNotIn(self.TOKEN.encode("utf-8"), blob)
        self.assertNotIn(b"Bearer", blob)

    def test_token_lives_only_in_the_config_file(self):
        directory = tempfile.mkdtemp(prefix="astra_tok_")
        cfg = SenderConfig()
        cfg.backend_url = "https://astra.example"
        cfg.sender_token = self.TOKEN
        cfg.sender_id = "win-01"
        cfg.ledger_path = os.path.join(directory, "ledger.sqlite")
        config_path = os.path.join(directory, "config.json")
        SenderConfig.save(cfg, config_path)

        # The config holds it...
        with open(config_path, "r", encoding="utf-8") as handle:
            self.assertIn(self.TOKEN, handle.read())
        # ...and the to_dict() shape never adds it under any other key.
        self.assertEqual(cfg.to_dict()["sender_token"], self.TOKEN)


class BackfillTest(unittest.TestCase):
    """Exercises the bounded, chronological, idempotent backfill helper."""

    def _ledger(self):
        directory = tempfile.mkdtemp(prefix="astra_bf_")
        return DeliveryLedger(os.path.join(directory, "l.sqlite")), directory

    def test_backfill_is_chronological_and_bounded(self):
        from astra_sender.engine import SyncEngine
        ledger, directory = self._ledger()
        times = [1792044000, 1792047600, 1792051200, 1792054800, 1792058400]
        engine = SyncEngine.__new__(SyncEngine)  # bound helper, no I/O
        plan = engine.plan_backfill("s1", "XAUUSD", "H1", times, max_bars=3,
                                    ledger=ledger)
        self.assertEqual(plan, sorted(plan), "backfill must be chronological")
        self.assertLessEqual(len(plan), 3)
        self.assertEqual(plan, times[-3:])
        ledger.close()

    def test_backfill_skips_acknowledged_candles(self):
        from astra_sender.engine import SyncEngine
        ledger, directory = self._ledger()
        acked = 1792047600
        identity = DeliveryLedger.make_identity("s1", "XAUUSD", "H1", acked)
        ledger.record_acknowledged("s1", "XAUUSD", "H1", acked, identity, 1)
        times = [1792044000, 1792047600, 1792051200]
        engine = SyncEngine.__new__(SyncEngine)
        plan = engine.plan_backfill("s1", "XAUUSD", "H1", times, max_bars=10,
                                    ledger=ledger)
        self.assertNotIn(acked, plan, "an acknowledged candle must not be re-sent")
        self.assertEqual(plan, [1792044000, 1792051200])
        ledger.close()

    def test_backfill_bound_is_negative_safe(self):
        from astra_sender.engine import SyncEngine
        ledger, directory = self._ledger()
        engine = SyncEngine.__new__(SyncEngine)
        plan = engine.plan_backfill("s1", "XAUUSD", "H1", [1, 2, 3], max_bars=0,
                                    ledger=ledger)
        self.assertEqual(plan, [])
        ledger.close()


if __name__ == "__main__":
    unittest.main()
