"""Tests for the standalone MT5 diagnostic (diag_mt5.py).

The diagnostic runs on the owner's PC, so it must never crash: it has to
report a missing package, a failed initialize, and a broker with no gold
symbol as plain findings rather than tracebacks.
"""

import os
import sys
import unittest
from unittest import mock


class _Info:
    def __init__(self, **kw):
        self.__dict__.update(kw)


def _fake_mt5(initialize_ok=True, terminal_ok=True, symbols=("XAUUSD",)):
    m = mock.MagicMock()
    m.__version__ = "5.0.0-test"
    m.initialize.return_value = initialize_ok
    m.last_error.return_value = (10004, "no ipc connection")
    m.terminal_info.return_value = _Info(
        connected=True, trade_allowed=True, build=4000, company="TestBroker"
    ) if terminal_ok else None
    m.account_info.return_value = _Info(login=1, server="Test-Demo")

    def symbol_info(name):
        return _Info(name=name, visible=True) if name in symbols else None

    m.symbol_info.side_effect = symbol_info

    def symbols_get(pattern=None):
        # Real MT5 filters on the pattern; a fake that ignores it would hide
        # bugs in the discovery fallback.
        if not pattern:
            return [_Info(name=n) for n in symbols]
        needle = pattern.strip("*").upper()
        return [_Info(name=n) for n in symbols if needle in n.upper()]

    m.symbols_get.side_effect = symbols_get
    m.copy_rates_from_pos.return_value = [
        {"time": 1750000000, "open": 2000.0, "high": 2005.0, "low": 1995.0,
         "close": 2002.0, "tick_volume": 100, "spread": 20, "real_volume": 0}
    ]
    return m


class DiagMt5Test(unittest.TestCase):
    def _run(self, fake):
        sys.modules.pop("diag_mt5", None)
        with mock.patch.dict(sys.modules, {"MetaTrader5": fake}):
            import diag_mt5
            return diag_mt5.main()

    def test_reports_missing_package_honestly(self):
        """A missing package is a finding, not a crash.

        ``sys.modules['MetaTrader5'] = None`` makes ``import MetaTrader5``
        raise ImportError, which is what a clean machine without the package
        looks like.
        """
        sys.modules.pop("diag_mt5", None)
        with mock.patch.dict(sys.modules, {"MetaTrader5": None}):
            import diag_mt5
            self.assertEqual(diag_mt5.main(), 2)

    def test_failed_initialize_returns_nonzero(self):
        fake = _fake_mt5(initialize_ok=False)
        self.assertEqual(self._run(fake), 4)

    def test_reads_closed_bar_not_forming_bar(self):
        """The diagnostic must request position 1, never position 0."""
        fake = _fake_mt5()
        self._run(fake)
        for call in fake.copy_rates_from_pos.call_args_list:
            self.assertEqual(call.args[2], 1,
                             "position 0 is the forming candle and must not be read")

    def test_no_gold_symbol_is_reported(self):
        fake = _fake_mt5(symbols=("EURUSD",))
        self.assertEqual(self._run(fake), 5)

    def test_happy_path_exits_zero(self):
        fake = _fake_mt5()
        self.assertEqual(self._run(fake), 0)


if __name__ == "__main__":
    unittest.main()
