"""Tests for the Windows privilege-level helpers.

The comparison only means something on Windows, so these assert the honest
degradation everywhere else plus the mismatch message that the GUI and the
diagnostic show to the owner.
"""

import os
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")))

from astra_sender import win_elevation  # noqa: E402


class ElevationTest(unittest.TestCase):
    def test_is_elevated_is_false_off_windows(self):
        """A non-Windows host must report not-elevated, not raise."""
        if os.name != "nt":
            self.assertFalse(win_elevation.is_elevated())

    def test_terminal_elevation_is_unknown_off_windows(self):
        """Unknown is None, which callers must render as 'unknown', not False."""
        if os.name != "nt":
            self.assertIsNone(win_elevation.terminal_elevation())

    def test_no_mismatch_message_when_terminal_level_is_unknown(self):
        """A None terminal level must not produce a false mismatch warning."""
        with mock.patch.object(win_elevation, "is_elevated", return_value=False), \
             mock.patch.object(win_elevation, "terminal_elevation",
                               return_value=None):
            self.assertEqual(win_elevation.describe_mismatch(), "")

    def test_no_mismatch_message_when_levels_match(self):
        for ours in (True, False):
            with mock.patch.object(win_elevation, "is_elevated",
                                   return_value=ours), \
                 mock.patch.object(win_elevation, "terminal_elevation",
                                   return_value=ours):
                self.assertEqual(win_elevation.describe_mismatch(), "")

    def test_mismatch_message_names_both_levels(self):
        """An elevated terminal with a normal sender must be called out."""
        with mock.patch.object(win_elevation, "is_elevated",
                               return_value=False), \
             mock.patch.object(win_elevation, "terminal_elevation",
                               return_value=True):
            message = win_elevation.describe_mismatch()
        self.assertIn("PRIVILEGE MISMATCH", message)
        self.assertIn("not elevated", message)
        self.assertIn("MetaTrader 5 is elevated", message)

    def test_mismatch_message_covers_the_elevated_sender_case(self):
        """The installer-launched case: sender elevated, terminal normal."""
        with mock.patch.object(win_elevation, "is_elevated",
                               return_value=True), \
             mock.patch.object(win_elevation, "terminal_elevation",
                               return_value=False):
            message = win_elevation.describe_mismatch()
        self.assertIn("PRIVILEGE MISMATCH", message)
        self.assertIn("ASTRA Sender is elevated", message)


class InstallerPrivilegeTest(unittest.TestCase):
    """The installer must not launch the sender elevated.

    Setup elevates to write Program Files/ProgramData. Without the
    runasoriginaluser flag its postinstall [Run] entry would start the app
    elevated too, which breaks the MT5 IPC handshake against a normally-started
    terminal - a failure that looks exactly like 'MT5 UNAVAILABLE'.
    """

    def _iss(self):
        path = os.path.normpath(os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "..", "installer",
            "astra_sender.iss"))
        with open(path, encoding="utf-8") as handle:
            return handle.read()

    def test_run_entry_uses_runasoriginaluser(self):
        iss = self._iss()
        self.assertIn("runasoriginaluser", iss)

    def test_run_entry_still_launches_the_app(self):
        """Guard against deleting the launch instead of de-elevating it."""
        iss = self._iss()
        self.assertIn("[Run]", iss)
        self.assertIn("{app}\\{#AppExeName}", iss)


if __name__ == "__main__":
    unittest.main()
