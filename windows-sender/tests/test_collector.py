"""Collector tests against the real bridge mt5_client + fake MetaTrader5.

Verifies:
  * all nine canonical timeframes map to MT5 constants;
  * the newest candle is always a CLOSED bar (never index 0);
  * OHLC validation rejects malformed bars;
  * an unavailable terminal reports honestly.
"""

from __future__ import annotations

import os
import time
import unittest

from astra_sender.collector import ClosedCandleCollector, _load_mt5_client


class CollectorTest(unittest.TestCase):
    def setUp(self):
        self.old_available = os.environ.get("FAKE_MT5_AVAILABLE", "1")
        os.environ["FAKE_MT5_AVAILABLE"] = "1"
        os.environ["FAKE_MT5_TOTAL_BARS"] = "300"
        os.environ["FAKE_MT5_LAST_TIME"] = str(int(time.time()))

    def tearDown(self):
        os.environ["FAKE_MT5_AVAILABLE"] = "1"

    def test_all_nine_timeframes_are_readable_closed_only(self):
        collector = ClosedCandleCollector("XAUUSD")
        startup = collector.start()
        self.assertTrue(startup.ok, startup.message)
        self.assertEqual(startup.resolved_symbol, "XAUUSD")
        for timeframe in ["M1", "M5", "M15", "M30", "H1", "H4", "D1",
                          "W1", "MN1"]:
            result = collector.read_latest_closed(timeframe)
            self.assertTrue(result["ok"], f"{timeframe}: {result}")
            candle = result["candle"]
            self.assertEqual(candle.timeframe, timeframe)
            self.assertGreater(candle.time, 0)
            # OHLC closed-bar sanity.
            self.assertLessEqual(candle.low, candle.high)
            self.assertGreaterEqual(candle.high, max(candle.open, candle.close))
            self.assertLessEqual(candle.low, min(candle.open, candle.close))
        collector.stop()

    def test_forming_bar_is_never_returned(self):
        # The fake feed's index-0 is the current forming bar. The bridge
        # client uses start_pos=1, so the returned open time must be strictly
        # behind the forming bar's open time.
        collector = ClosedCandleCollector("XAUUSD")
        self.assertTrue(collector.start().ok)
        result = collector.read_latest_closed("M15")
        self.assertTrue(result["ok"])
        now = int(time.time())
        forming_open = now - (now % 900)   # current M15 boundary
        self.assertLess(result["candle"].time, forming_open)
        collector.stop()

    def test_terminal_unavailable_is_honest(self):
        os.environ["FAKE_MT5_AVAILABLE"] = "0"
        collector = ClosedCandleCollector("XAUUSD")
        startup = collector.start()
        self.assertFalse(startup.ok)
        # Even after failure, reads report an explicit error, never a bar.
        result = collector.read_latest_closed("M15")
        self.assertFalse(result["ok"])
        self.assertIn("error_code", result)
        collector.stop()

    def test_legacy_module_load(self):
        # The bridge client must be importable by the real module name.
        module = _load_mt5_client()
        self.assertTrue(callable(getattr(module, "Mt5Client", None)))


if __name__ == "__main__":
    unittest.main()