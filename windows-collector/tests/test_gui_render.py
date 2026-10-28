"""Tests for the desktop window's rendering logic.

tkinter is not importable on a headless build machine, so the window itself is
not constructed here. What is tested is the part that can silently lie to the
user: how a status snapshot is turned into displayed text. A missing value must
render as a dash, never as a zero or a plausible-looking timestamp.
"""

from __future__ import annotations

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from collector.display import colour as _colour, fmt_count, fmt_time as _fmt_time  # noqa: E402


class FormatTest(unittest.TestCase):
    def test_missing_time_is_a_dash_not_a_date(self):
        self.assertEqual(_fmt_time(None), "—")
        self.assertEqual(_fmt_time(0), "—")
        self.assertEqual(_fmt_time(""), "—")

    def test_real_time_is_utc(self):
        self.assertEqual(_fmt_time(1700000000), "2023-11-14 22:13:20Z")

    def test_missing_count_is_a_dash_not_a_zero(self):
        self.assertEqual(fmt_count(None), "—")
        self.assertEqual(fmt_count(0), "0")

    def test_states_map_to_distinct_colours(self):
        self.assertEqual(_colour("READY"), _colour("ONLINE"))
        self.assertNotEqual(_colour("READY"), _colour("ERROR"))
        self.assertNotEqual(_colour("STARTING"), _colour("ERROR"))
        self.assertEqual(_colour("SOMETHING_UNKNOWN"), _colour("STOPPED"))


if __name__ == "__main__":
    unittest.main()
