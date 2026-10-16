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


def _run_script(*args, extra_env=None):
    # Execute the file directly (not `-m`), exactly like the PyInstaller
    # bootloader. cwd is the package parent so `astra_sender` is importable.
    # ASTRA_SENDER_NO_DIALOG keeps a startup message box from blocking: the
    # test environment has no console, and a native dialog would wait forever
    # for a click that never comes.
    env = dict(os.environ)
    env["ASTRA_SENDER_NO_DIALOG"] = "1"
    if extra_env:
        env.update(extra_env)
    return subprocess.run(
        [sys.executable, _MAIN, *args],
        cwd=_PKG_DIR, capture_output=True, text=True, timeout=120, env=env,
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

    def test_windowed_mode_writes_log_instead_of_failing_silently(self):
        # A PyInstaller windowed exe has sys.stdout is None and sys.stderr is
        # None. The old code did `print(..., file=sys.stderr)`, which discarded
        # the message AND raised, so double-clicking with no config produced no
        # window, no dialog and no log — indistinguishable from "nothing
        # happened". Simulate that environment and require the failure to be
        # captured on disk.
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            env = dict(os.environ)
            env["LOCALAPPDATA"] = tmp
            env["XDG_CONFIG_HOME"] = tmp
            # No real user to click a message box, so disable it: the point of
            # this test is the log file, not the dialog.
            env["ASTRA_SENDER_NO_DIALOG"] = "1"
            code = (
                "import sys\n"
                "sys.stdout = None\n"
                "sys.stderr = None\n"
                "sys.path.insert(0, %r)\n"
                "import astra_sender.__main__ as m\n"
                "raise SystemExit(m.main())\n" % _PKG_DIR
            )
            result = subprocess.run([sys.executable, "-c", code],
                                    capture_output=True, text=True,
                                    timeout=60, env=env)
            # Exit 2 = "not configured" is the correct outcome; a crash (1)
            # would mean the guard fired instead.
            self.assertEqual(result.returncode, 2, result.stderr)

            log = os.path.join(tmp, "astra_sender" if sys.platform != "win32"
                               else "AstraSender", "sender.log")
            self.assertTrue(os.path.exists(log),
                            "windowed startup failure left no sender.log")
            with open(log, encoding="utf-8") as fh:
                body = fh.read()
            self.assertIn("not configured", body)


if __name__ == "__main__":
    unittest.main()
