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


class BridgePackagingTest(unittest.TestCase):
    """Pin the packaging fault that made the frozen app report MT5 UNAVAILABLE.

    The bridge is a flat module pair: ``mt5_client`` does
    ``from schemas import TIMEFRAMES``. PyInstaller shipped both files *inside*
    the ``astra_sender`` package, so that import resolved only when the modules
    were imported as ``astra_sender.mt5_client`` — and even then ``schemas`` was
    not importable, because it landed in the package rather than on ``sys.path``.
    The loader swallowed the resulting ImportError, so the owner saw a bare
    "UNAVAILABLE" while a plain Python script worked fine on the same PC.

    These tests fail if either half of the fix regresses: the layout in the spec,
    or the loader's willingness to report why it could not import.
    """

    def _repo_root(self) -> str:
        return os.path.normpath(
            os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

    def test_spec_bundles_bridge_at_bundle_root(self):
        """The spec must place the bridge on sys.path, not inside the package."""
        spec_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                 "..", "astra_sender.spec")
        with open(spec_path, encoding="utf-8") as fh:
            spec = fh.read()
        self.assertIn('"mt5_client.py"), "."', spec)
        self.assertIn('"schemas.py"), "."', spec)
        self.assertNotIn('"mt5_client.py"), "astra_sender"', spec)
        self.assertNotIn('"schemas.py"), "astra_sender"', spec)

    def test_bridge_resolves_schemas_as_a_flat_sibling(self):
        """mt5_client must import schemas as a top-level module."""
        bridge_dir = os.path.join(self._repo_root(), "bridge", "mt5_python")
        source = open(os.path.join(bridge_dir, "mt5_client.py"),
                      encoding="utf-8").read()
        self.assertIn("from schemas import", source)

    def test_loader_reports_the_underlying_import_error(self):
        """A failed load must name the cause, never a bare 'cannot locate'."""
        import sys as _sys
        import astra_sender.collector as collector_mod

        saved = (collector_mod._BRIDGE_MODULE_NAME, collector_mod._MT5_CLIENT_NAMES,
                 collector_mod._BRIDGE_HINTS, _sys.modules.pop("mt5_client", None))
        try:
            collector_mod._BRIDGE_MODULE_NAME = "definitely_not_a_module"
            collector_mod._MT5_CLIENT_NAMES = ("definitely_not_a_module",)
            collector_mod._BRIDGE_HINTS = ["/nonexistent/bridge/dir"]
            with self.assertRaises(ImportError) as ctx:
                collector_mod._load_mt5_client()
            message = str(ctx.exception)
            self.assertIn("definitely_not_a_module", message)
            self.assertIn("No module named", message)
        finally:
            (collector_mod._BRIDGE_MODULE_NAME, collector_mod._MT5_CLIENT_NAMES,
             collector_mod._BRIDGE_HINTS, restored) = saved
            if restored is not None:
                _sys.modules["mt5_client"] = restored

    def test_frozen_bundle_root_is_a_search_hint(self):
        """_MEIPASS must be searched when frozen, so the layout is not implicit."""
        import sys as _sys
        import astra_sender.collector as collector_mod

        meipass = getattr(_sys, "_MEIPASS", None)
        if meipass:
            self.assertIn(meipass, collector_mod._BRIDGE_HINTS)
        else:
            # Not frozen: the hint list must still be non-empty and absolute.
            self.assertTrue(collector_mod._BRIDGE_HINTS)
            self.assertTrue(all(os.path.isabs(h) for h in collector_mod._BRIDGE_HINTS))


if __name__ == "__main__":
    unittest.main()
