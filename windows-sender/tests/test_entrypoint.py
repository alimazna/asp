"""Regression test for the frozen-app entry point.

PyInstaller runs ``astra_sender/__main__.py`` as a top-level script, where
relative imports (``from .config import ...``) raise
``ImportError: attempted relative import with no known parent package``. That
crashed the packaged ``AstraSender.exe`` on real startup while the old
``--version`` CI smoke test kept passing, because ``--version`` returns before
the relative imports are reached.

These tests run the module the same way the frozen bootloader does and assert
that it starts cleanly.
"""

from __future__ import annotations

import os
import subprocess
import sys
import unittest

_PKG_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_MAIN = os.path.join(_PKG_DIR, "astra_sender", "__main__.py")
_RELATIVE_IMPORT_ERROR = "attempted relative import with no known parent package"


def _run_script(*args):
    # Execute the file directly (not `-m`), exactly like the PyInstaller
    # bootloader. cwd is the package parent so `astra_sender` is importable.
    return subprocess.run(
        [sys.executable, _MAIN, *args],
        cwd=_PKG_DIR, capture_output=True, text=True, timeout=120,
    )


class EntryPointTest(unittest.TestCase):
    def test_script_style_version(self):
        result = _run_script("--version")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("AstraSender", result.stdout)

    def test_script_style_selfcheck_imports_all_modules(self):
        result = _run_script("--selfcheck")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn(_RELATIVE_IMPORT_ERROR, result.stderr)
        self.assertIn("self-check OK", result.stdout)

    def test_script_style_real_startup_never_raises_relative_import(self):
        # No config file: a clean "not configured" exit (2) is correct. What
        # must never happen is the ImportError traceback.
        result = _run_script()
        self.assertNotIn(_RELATIVE_IMPORT_ERROR, result.stderr)
        self.assertIn(result.returncode, (0, 2), result.stderr)


if __name__ == "__main__":
    unittest.main()
