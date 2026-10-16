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


class DiagPrivilegeAndErrorReportingTest(unittest.TestCase):
    """The diagnostic must name the failure, not just fail.

    Two real support cases drove these tests:
      * a failed attach that printed no ``last_error()``, leaving nothing to act
        on, and
      * a failed attach that never returned, because MT5's own IPC default is
        60000 ms, so the tool looked like it had hung.
    """

    def _run_capture(self, fake):
        sys.modules.pop("diag_mt5", None)
        import io
        from contextlib import redirect_stdout
        buf = io.StringIO()
        with mock.patch.dict(sys.modules, {"MetaTrader5": fake}):
            import diag_mt5
            with redirect_stdout(buf):
                code = diag_mt5.main()
        return code, buf.getvalue()

    def test_prints_raw_last_error_code_on_failure(self):
        """-10005 is the privilege-mismatch code; it must appear verbatim."""
        fake = _fake_mt5(initialize_ok=False)
        fake.last_error.return_value = (-10005, "IPC timeout")
        code, out = self._run_capture(fake)
        self.assertEqual(code, 4)
        self.assertIn("-10005", out, "the raw MT5 code must be printed")
        self.assertIn("IPC timeout", out)
        self.assertIn("Error code -10005", out)

    def test_prints_privilege_level_guidance(self):
        fake = _fake_mt5(initialize_ok=False)
        fake.last_error.return_value = (-10005, "IPC timeout")
        _, out = self._run_capture(fake)
        self.assertIn("PRIVILEGE LEVEL CHECK", out)
        self.assertIn("SAME level", out)

    def test_passes_a_bounded_timeout_to_initialize(self):
        """An unbounded attach is what made the tool appear to hang."""
        fake = _fake_mt5()
        self._run_capture(fake)
        self.assertTrue(fake.initialize.called)
        kwargs = fake.initialize.call_args.kwargs
        self.assertIn("timeout", kwargs)
        self.assertGreater(kwargs["timeout"], 0)

    def test_blocking_initialize_does_not_hang(self):
        """A terminal that never answers must produce a finding, not a hang."""
        import time as _time
        fake = _fake_mt5()
        fake.initialize.side_effect = lambda **kw: _time.sleep(120)
        sys.modules.pop("diag_mt5", None)
        import io
        from contextlib import redirect_stdout
        buf = io.StringIO()
        with mock.patch.dict(sys.modules, {"MetaTrader5": fake}):
            import diag_mt5
            # Shrink the ceilings so the test does not wait the real 15s + 30s.
            diag_mt5._INIT_TIMEOUT_MS = 200
            diag_mt5._INIT_WALL_CLOCK_GRACE_SECONDS = 0.5
            started = _time.time()
            with redirect_stdout(buf):
                code = diag_mt5.main()
            elapsed = _time.time() - started
        self.assertEqual(code, 4, "a timeout is a failed attach")
        self.assertLess(elapsed, 10, "the diagnostic must not hang")
        self.assertIn("TIMED OUT", buf.getvalue())


class DiagMt5PathTest(unittest.TestCase):
    """The bridge must be able to target a specific terminal64.exe."""

    def _bridge(self):
        sys.path.insert(0, os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            "..", "bridge", "mt5_python"))
        import importlib
        import mt5_client
        return importlib.reload(mt5_client)

    def test_initialize_passes_path_positionally(self):
        client_mod = self._bridge()
        fake = mock.MagicMock()
        fake.initialize.return_value = True
        fake.version.return_value = (500, 0, 0)
        fake.account_info.return_value = None
        with mock.patch.object(client_mod, "_mt5", fake):
            client = client_mod.Mt5Client()
            result = client.initialize(
                path=r"C:\Program Files\MetaTrader 5\terminal64.exe",
                timeout_ms=5000)
        self.assertTrue(result.ok)
        args, kwargs = fake.initialize.call_args
        # MT5 takes the path as the first positional argument.
        self.assertEqual(args[0], r"C:\Program Files\MetaTrader 5\terminal64.exe")
        self.assertEqual(kwargs.get("timeout"), 5000)

    def test_initialize_without_path_uses_auto_discovery(self):
        client_mod = self._bridge()
        fake = mock.MagicMock()
        fake.initialize.return_value = True
        fake.version.return_value = (500, 0, 0)
        fake.account_info.return_value = None
        with mock.patch.object(client_mod, "_mt5", fake):
            client = client_mod.Mt5Client()
            client.initialize(timeout_ms=1000)
        args, kwargs = fake.initialize.call_args
        self.assertEqual(args, (), "no path must be passed for auto-discovery")
        self.assertEqual(kwargs.get("timeout"), 1000)

    def test_failed_initialize_reports_the_raw_error(self):
        client_mod = self._bridge()
        fake = mock.MagicMock()
        fake.initialize.return_value = False
        fake.last_error.return_value = (-10005, "IPC timeout")
        with mock.patch.object(client_mod, "_mt5", fake):
            client = client_mod.Mt5Client()
            result = client.initialize(timeout_ms=1000)
        self.assertFalse(result.ok)
        self.assertIn("-10005", result.message)
        self.assertIn("1000", result.message)


class DiagReportPathTest(unittest.TestCase):
    """`--diag-mt5` must write its report where the GUI opens the folder.

    The CI smoke test asserts the report exists at %PROGRAMDATA%\\AstraSender
    because that is where the per-machine installer puts the config. An earlier
    build wrote to %LOCALAPPDATA% instead and the check failed, so the path is
    pinned here rather than left to drift.
    """

    def test_report_is_written_beside_the_resolved_config(self):
        import tempfile
        from astra_sender import __main__ as m

        with tempfile.TemporaryDirectory() as tmp:
            cfg_path = os.path.join(tmp, "config.json")
            with mock.patch.object(m, "_run_mt5_diagnostic", wraps=m._run_mt5_diagnostic):
                with mock.patch("astra_sender.config.default_config_path",
                                return_value=cfg_path):
                    with mock.patch.dict(sys.modules, {"MetaTrader5": None}):
                        code = m._run_mt5_diagnostic()
            report = os.path.join(tmp, "mt5_diag.txt")
            self.assertTrue(os.path.exists(report),
                            "report was not written beside the config")
            with open(report, encoding="utf-8") as fh:
                body = fh.read()
            self.assertIn("ASTRA Sender - MT5 diagnostic", body)
            # No package on this machine, so a non-zero finding is correct.
            self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()
