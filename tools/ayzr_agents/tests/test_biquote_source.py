#!/usr/bin/env python3
"""Data-quality tests for the BiQuote candle source.

BiQuote is the approved operational XAUUSD source (see docs/ayzr/BIQUOTE.md).
These tests exercise the REAL sender's pure functions and its failure handling,
with the network replaced by an injected stub, so they consume no BiQuote quota
and never touch the internet.

Run:  python3 tools/ayzr_agents/tests/test_biquote_source.py
"""
import inspect
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
AGENTS = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, AGENTS)

import biquote_sender as bq  # noqa: E402


def bar(open_time, o, h, l, c, is_open=False, tick_volume=10):
    return {"openTime": open_time, "open": o, "high": h, "low": l, "close": c,
            "isOpen": is_open, "tickVolume": tick_volume}


class TimezoneHandling(unittest.TestCase):
    """Bar stamps are UTC and must be read as UTC, never as local time."""

    def test_parses_utc_z_suffix(self):
        # 2026-10-30T07:00:00Z -> a fixed epoch, independent of the test host TZ.
        self.assertEqual(bq.parse_iso_utc_to_epoch("2026-10-30T07:00:00Z"),
                         1793343600)

    def test_parses_fractional_seconds(self):
        self.assertEqual(bq.parse_iso_utc_to_epoch("2026-10-30T07:00:00.123Z"),
                         1793343600)

    def test_offset_is_always_zero(self):
        # biquote stamps UTC; the sender must declare no broker-clock shift.
        src = inspect.getsource(bq.send_batch)
        self.assertIn('"clock_offset_seconds": 0', src)


class ClosedBarSelection(unittest.TestCase):
    def test_open_bar_is_never_sent(self):
        bars = [bar("2026-10-30T07:00:00Z", 1, 2, 0.5, 1.5, is_open=True),
                bar("2026-10-30T06:45:00Z", 1, 2, 0.5, 1.5)]
        out = bq.to_astra_candles(bars, "M15", 0)
        self.assertEqual([c["time"] for c in out], [1793342700],
                         "the in-progress bar must be dropped")

    def test_only_strictly_newer_than_watermark(self):
        bars = [bar("2026-10-30T07:00:00Z", 1, 2, 0.5, 1.5),
                bar("2026-10-30T06:45:00Z", 1, 2, 0.5, 1.5)]
        watermark = bq.parse_iso_utc_to_epoch("2026-10-30T06:45:00Z")
        out = bq.to_astra_candles(bars, "M15", watermark)
        self.assertEqual([c["time"] for c in out], [1793343600],
                         "a bar equal to the watermark must not be re-sent")

    def test_output_is_oldest_first(self):
        bars = [bar("2026-10-30T07:00:00Z", 1, 2, 0.5, 1.5),
                bar("2026-10-30T06:30:00Z", 1, 2, 0.5, 1.5),
                bar("2026-10-30T06:45:00Z", 1, 2, 0.5, 1.5)]
        out = bq.to_astra_candles(bars, "M15", 0)
        times = [c["time"] for c in out]
        self.assertEqual(times, sorted(times))

    def test_uid_is_stable_and_identifying(self):
        bars = [bar("2026-10-30T07:00:00Z", 1, 2, 0.5, 1.5)]
        a = bq.to_astra_candles(bars, "M15", 0)[0]["uid"]
        b = bq.to_astra_candles(bars, "M15", 0)[0]["uid"]
        self.assertEqual(a, b)
        self.assertEqual(a, "XAUUSD-M15-1793343600")


class OhlcFidelity(unittest.TestCase):
    def test_values_pass_through_unchanged(self):
        bars = [bar("2026-10-30T07:00:00Z", 2001.5, 2005.25, 1999.0, 2003.75)]
        c = bq.to_astra_candles(bars, "M15", 0)[0]
        self.assertEqual((c["open"], c["high"], c["low"], c["close"]),
                         (2001.5, 2005.25, 1999.0, 2003.75))

    def test_volume_defaults_to_zero_when_absent(self):
        b = bar("2026-10-30T07:00:00Z", 1, 2, 0.5, 1.5)
        del b["tickVolume"]
        c = bq.to_astra_candles([b], "M15", 0)[0]
        self.assertEqual(c["tick_volume"], 0)


class FetchErrorHandling(unittest.TestCase):
    """fetch_bars must fail loudly on a bad response, never invent bars."""

    def _patch(self, fn):
        orig = bq.http_json
        bq.http_json = fn
        self.addCleanup(lambda: setattr(bq, "http_json", orig))

    def test_http_200_with_bars(self):
        self._patch(lambda url, timeout=25: (200, {"bars": [bar("x", 1, 2, 0.5, 1.5)]}))
        self.assertEqual(len(bq.fetch_bars("15m", 10)), 1)

    def test_non_200_raises(self):
        self._patch(lambda url, timeout=25: (429, {"error": "rate limited"}))
        with self.assertRaises(RuntimeError):
            bq.fetch_bars("15m", 10)

    def test_missing_bars_key_raises(self):
        self._patch(lambda url, timeout=25: (200, {"status": "ok"}))
        with self.assertRaises(RuntimeError):
            bq.fetch_bars("15m", 10)

    def test_network_exception_propagates(self):
        def boom(url, timeout=25):
            raise OSError("connection reset")
        self._patch(boom)
        with self.assertRaises(OSError):
            bq.fetch_bars("15m", 10)


class TimeframeMapping(unittest.TestCase):
    def test_seven_operational_timeframes(self):
        self.assertEqual(set(bq.TIMEFRAME_TO_INTERVAL),
                         {"M1", "M5", "M15", "M30", "H1", "H4", "D1"})

    def test_w1_and_mn1_are_absent(self):
        # biquote does not offer weekly/monthly; the sender must not pretend to.
        self.assertNotIn("W1", bq.TIMEFRAME_TO_INTERVAL)
        self.assertNotIn("MN1", bq.TIMEFRAME_TO_INTERVAL)

    def test_send_order_feeds_structure_first(self):
        # D1/H4 must precede M15 so the multi-timeframe context exists when the
        # operational bar arrives.
        order = bq.SEND_ORDER
        self.assertLess(order.index("D1"), order.index("M15"))
        self.assertLess(order.index("H4"), order.index("M15"))


class BaseUrlNormalization(unittest.TestCase):
    def test_no_double_api_prefix(self):
        for base in ("http://h", "http://h/api", "http://h/api/v1",
                     "http://h/api/v1/"):
            with self.subTest(base=base):
                self.assertEqual(bq.normalize_base(base), "http://h")


class WatermarkAndFailureSemantics(unittest.TestCase):
    """A failed batch must not advance the watermark; a good one must."""

    def _args(self, **kw):
        class A:
            base_url = "http://127.0.0.1:9"
            token = "t"
            source_id = "biquote"
            backfill = 10
            catchup = 10
            dry_run = False
            supabase_url = ""
            supabase_key = ""
            supabase_table = "candles"
        for k, v in kw.items():
            setattr(A, k, v)
        return A()

    def _bars(self):
        return [bar("2026-10-30T07:00:00Z", 1, 2, 0.5, 1.5)]

    def _patch(self, **kw):
        originals = {name: getattr(bq, name) for name in kw}
        for name, fn in kw.items():
            setattr(bq, name, fn)
        self.addCleanup(lambda: [setattr(bq, n, f) for n, f in originals.items()])

    def test_accepted_batch_advances_watermark(self):
        self._patch(fetch_bars=lambda interval, limit, timeout=25: self._bars(),
                    send_batch=lambda *a, **k: (200, {"ingested": 1,
                                                      "duplicates": [],
                                                      "rejected": []}))
        state = {}
        ok = bq.run_once(self._args(), state)
        self.assertTrue(ok)
        self.assertEqual(state["D1"], 1793343600)

    def test_failed_send_does_not_advance_watermark(self):
        def boom(*a, **k):
            raise OSError("backend down")
        self._patch(fetch_bars=lambda interval, limit, timeout=25: self._bars(),
                    send_batch=boom)
        state = {}
        ok = bq.run_once(self._args(), state)
        self.assertFalse(ok)
        self.assertEqual(state, {}, "nothing persisted -> watermark unchanged")

    def test_rejected_batch_does_not_advance_watermark(self):
        self._patch(fetch_bars=lambda interval, limit, timeout=25: self._bars(),
                    send_batch=lambda *a, **k: (200, {"ingested": 0,
                                                      "duplicates": [],
                                                      "rejected": [{"reason": "bad"}]}))
        state = {}
        ok = bq.run_once(self._args(), state)
        self.assertFalse(ok)
        self.assertEqual(state, {})

    def test_one_timeframe_failure_does_not_stop_the_others(self):
        def fetch(interval, limit, timeout=25):
            if interval == "1d":  # D1 is first in SEND_ORDER
                raise OSError("d1 feed down")
            return self._bars()
        self._patch(fetch_bars=fetch,
                    send_batch=lambda *a, **k: (200, {"ingested": 1,
                                                      "duplicates": [],
                                                      "rejected": []}))
        state = {}
        ok = bq.run_once(self._args(), state)
        self.assertFalse(ok, "the D1 failure must be reported")
        self.assertNotIn("D1", state)
        self.assertIn("H4", state, "later timeframes must still be fed")

    def test_no_new_bars_is_not_a_failure(self):
        self._patch(fetch_bars=lambda interval, limit, timeout=25: [])
        state = {"D1": 999}
        ok = bq.run_once(self._args(), state)
        self.assertTrue(ok, "an empty window is a normal no-op, not an error")


if __name__ == "__main__":
    unittest.main(verbosity=2)
