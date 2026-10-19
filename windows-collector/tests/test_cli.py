"""End-to-end CLI test: run the real CLI against the doubles.

Verifies the whole path — argument parsing, config persistence, token handling,
MT5 read, planning, delivery, and the printed status — with only the MT5
terminal and the receiving endpoint replaced by test doubles.
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)

from doubles import FakeReceiver  # noqa: E402


def _make_mt5_shim(directory: str) -> None:
    """A fake MetaTrader5 module so the real reader runs against the double."""
    source = f'''"""Fake MetaTrader5 for the end-to-end CLI test. Test data only."""
import sys
sys.path.insert(0, {HERE!r})
from doubles import FakeMt5Terminal

_terminal = FakeMt5Terminal(symbol="XAUUSD", total_bars=300)

TIMEFRAME_M1 = 1
TIMEFRAME_M5 = 5
TIMEFRAME_M15 = 15
TIMEFRAME_M30 = 30
TIMEFRAME_H1 = 16385
TIMEFRAME_H4 = 16388
TIMEFRAME_D1 = 16408
TIMEFRAME_W1 = 32769
TIMEFRAME_MN1 = 49153


def initialize(*args, **kwargs):
    return _terminal.available


def shutdown():
    return None


def last_error():
    return _terminal.last_error()


def symbol_info(symbol):
    return _terminal.symbol_info(symbol)


def symbol_info_tick(symbol):
    return _terminal.symbol_info_tick(symbol)


def copy_rates_from_pos(symbol, timeframe, start_pos, count):
    return _terminal.copy_rates_from_pos(symbol, timeframe, start_pos, count)
'''
    with open(os.path.join(directory, "MetaTrader5.py"), "w",
              encoding="utf-8") as handle:
        handle.write(source)


class CliEndToEndTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.receiver = FakeReceiver(token="cli-token")
        cls.shim_dir = tempfile.mkdtemp(prefix="astra_shim_")
        _make_mt5_shim(cls.shim_dir)
        cls.data_dir = tempfile.mkdtemp(prefix="astra_cli_")
        cls.env = dict(os.environ)
        cls.env["PYTHONPATH"] = cls.shim_dir + os.pathsep + ROOT
        cls.env["ASTRA_COLLECTOR_DIR"] = cls.data_dir
        cls.env["ASTRA_COLLECTOR_TOKEN"] = "cli-token"

    @classmethod
    def tearDownClass(cls):
        cls.receiver.stop()

    def _run(self, *args, stdin: str = ""):
        return subprocess.run(
            [sys.executable, "-m", "collector", *args],
            cwd=ROOT, env=self.env, capture_output=True, text=True,
            input=stdin, timeout=120)

    def test_01_version(self):
        result = self._run("--version")
        self.assertEqual(result.returncode, 0)
        self.assertIn("astra-collector", result.stdout)

    def test_02_configure_destination(self):
        result = self._run("--set-destination", self.receiver.url,
                           "--set-sender-id", "cli-sender",
                           "--set-symbol", "XAUUSD")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(os.path.isfile(
            os.path.join(self.data_dir, "config.json")))

    def test_03_show_config_never_prints_the_token(self):
        result = self._run("--show-config", "--json")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("cli-token", result.stdout,
                         "the token must never be printed")
        self.assertIn("token_configured", result.stdout)

    def test_04_single_pass_delivers_and_reports(self):
        result = self._run("--once", "--json", "--allow-insecure-http")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertGreater(len(self.receiver.state.stored), 0,
                           "the receiver must have received candles")
        self.assertIn("acknowledged", result.stdout)

    def test_05_second_pass_never_resends_an_older_candle(self):
        """A repeat pass may only add genuinely newer candles.

        A minute boundary can roll over between passes, which legitimately
        produces one new closed M1 bar. What must never happen is an older
        candle being re-delivered, so the invariant is checked on timestamps
        rather than on a raw count.
        """
        before = dict(self.receiver.state.stored)
        newest_before = max(int(c["open_time_utc"]) for c in before.values())
        self.receiver.state.requests = []
        result = self._run("--once", "--json", "--allow-insecure-http")
        self.assertEqual(result.returncode, 0, result.stderr)
        added = {uid: c for uid, c in self.receiver.state.stored.items()
                 if uid not in before}
        for uid, candle in added.items():
            self.assertGreater(
                int(candle["open_time_utc"]), newest_before,
                f"{uid} is older than the previous newest candle; a repeat "
                f"pass must never re-deliver an already-acknowledged candle")
        for uid in before:
            self.assertIn(uid, self.receiver.state.stored,
                          "an existing record must not be removed")

    def test_06_status_reports_real_state(self):
        result = self._run("--status", "--json", "--allow-insecure-http")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("destination", result.stdout)


if __name__ == "__main__":
    unittest.main()
