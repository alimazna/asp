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
        # No config file. This subprocess sets ASTRA_SENDER_NO_DIALOG=1 and is
        # headless, so there is nobody to type a token: a clean "not
        # configured" exit (2) is correct here. What must never happen is the
        # ImportError traceback. The interactive windowed path (which opens the
        # GUI with Settings ready) is covered by test_gui_headless.
        result = _run_script()
        self.assertNotIn(_RELATIVE_IMPORT_ERROR, result.stderr)
        self.assertEqual(result.returncode, 2, result.stderr)

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


class InteractiveFirstRunTest(unittest.TestCase):
    """An interactive windowed launch with no config must open the GUI.

    Regression: __main__ showed a modal box and returned 2 unconditionally, so
    a double-clicked windowed exe closed immediately and the owner had no way
    to enter the token - the app looked broken rather than unconfigured.
    """

    def test_interactive_windowed_launch_reaches_the_gui(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            marker = os.path.join(tmp, "gui_path.txt")
            env = dict(os.environ)
            env["LOCALAPPDATA"] = tmp
            env["XDG_CONFIG_HOME"] = tmp
            # Simulate a real double-click: no console, and dialogs allowed.
            env.pop("ASTRA_SENDER_NO_DIALOG", None)
            # A PyInstaller windowed exe has sys.stdout/stderr None, which is
            # what makes _install_console_fallback report "no console" and the
            # setup prompt treat the session as interactive. A fake
            # astra_sender.gui stands in for the Tk window: its run() records
            # that the GUI path was reached and returns, so no display is
            # needed and no network client is started.
            stub = (
                "import sys, types\n"
                "sys.stdout = None\n"
                "sys.stderr = None\n"
                "sys.path.insert(0, %r)\n"
                "tk = types.ModuleType('tkinter')\n"
                "class _W:\n"
                "    def __init__(self, *a, **k): pass\n"
                "tk.Tk = _W\n"
                "ttk = types.ModuleType('tkinter.ttk')\n"
                "for n in ('Frame', 'Label', 'LabelFrame', 'Button', 'Entry'):\n"
                "    setattr(ttk, n, _W)\n"
                "sys.modules.update({'tkinter': tk, 'tkinter.ttk': ttk})\n"
                "gui = types.ModuleType('astra_sender.gui')\n"
                "class SenderGui:\n"
                "    def __init__(self, *a, **k): pass\n"
                "    def run(self, on_status):\n"
                "        open(%r, 'w').write('reached')\n"
                "gui.SenderGui = SenderGui\n"
                "sys.modules['astra_sender.gui'] = gui\n"
                "import astra_sender.__main__ as m\n"
                "code = m.main()\n"
                "open(%r, 'a').write('|exit=%%d' %% code)\n"
                % (_PKG_DIR, marker, marker)
            )
            result = subprocess.run(
                [sys.executable, "-c", stub],
                capture_output=True, text=True, timeout=120, env=env)
            self.assertTrue(os.path.exists(marker),
                            f"the GUI was never opened; stderr={result.stderr[-600:]!r}")
            with open(marker, encoding="utf-8") as fh:
                body = fh.read()
            self.assertIn("reached", body)
            self.assertIn("exit=0", body,
                          "the interactive path should finish cleanly")


if __name__ == "__main__":
    unittest.main()
