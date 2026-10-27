"""Windows entry point for the packaged collector.

Double-clicking the executable opens the desktop window. Passing arguments runs
the command-line interface instead, so the same binary can be scripted, probed
by CI, and diagnosed without a separate build:

    ASTRA-Collector.exe                      -> the window
    ASTRA-Collector.exe --version            -> print the version
    ASTRA-Collector.exe --show-config        -> print the effective settings
    ASTRA-Collector.exe --status             -> print the current status
    ASTRA-Collector.exe --once               -> run a single pass and exit
    ASTRA-Collector.exe --set-destination U  -> save a new receiver URL
    ASTRA-Collector.exe --selfcheck          -> import every runtime module

A windowed (no-console) build has no stdout for a user to read, so a startup
failure is also written to `startup-error.log` in the data directory; that is
how a double-clicked diagnostic run remains recoverable.

Frozen by PyInstaller into a single executable so the owner never needs Python,
pip, or a command prompt.
"""

from __future__ import annotations

import sys


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)

    # No arguments is the normal case: open the window.
    if not argv:
        return _run_gui()

    # `--selfcheck` imports every runtime module and returns. It exists so CI
    # can catch a packaging error (a missing module, a broken relative import)
    # that `--version` alone would not, because `--version` returns before the
    # collector package is ever imported.
    if argv == ["--selfcheck"]:
        return _selfcheck()

    try:
        from collector.cli import main as cli_main
    except ImportError as exc:
        return _fail(f"the command-line interface is unavailable: {exc}")
    return cli_main(argv)


def _run_gui() -> int:
    try:
        from collector.gui import main as gui_main
    except ImportError as exc:
        # tkinter missing is the one failure worth naming precisely.
        return _fail(f"could not start the window. Missing component: {exc}")
    return gui_main()


def _selfcheck() -> int:
    """Import every module the frozen app needs at runtime."""
    modules = [
        "collector.cli", "collector.config", "collector.display",
        "collector.engine", "collector.gui", "collector.identity",
        "collector.ledger", "collector.mt5_reader", "collector.planner",
        "collector.receiver", "collector.store",
    ]
    failures = []
    for name in modules:
        try:
            __import__(name)
        except Exception as exc:  # noqa: BLE001 - report, do not mask
            failures.append(f"{name}: {type(exc).__name__}: {exc}")
    try:
        import tkinter  # noqa: F401
    except Exception as exc:  # noqa: BLE001
        failures.append(f"tkinter: {type(exc).__name__}: {exc}")
    # MetaTrader5 is only present on Windows and is imported lazily, so its
    # absence is not a packaging failure on any other host.
    if sys.platform == "win32":
        try:
            import MetaTrader5  # noqa: F401
        except Exception as exc:  # noqa: BLE001
            failures.append(f"MetaTrader5: {type(exc).__name__}: {exc}")

    if failures:
        sys.stderr.write("selfcheck failed:\n")
        for line in failures:
            sys.stderr.write(f"  - {line}\n")
        return 4
    sys.stdout.write("selfcheck: all runtime modules import cleanly\n")
    return 0


def _fail(message: str) -> int:
    """Report a startup failure, then try to leave a durable trace.

    A windowed build has no console, so stderr goes nowhere the owner can see.
    A log file in the data directory is the one place a double-clicked failure
    can still be read afterwards.
    """
    text = f"ASTRA Collector could not start.\n{message}\n"
    sys.stderr.write(text)
    try:
        import os
        import time
        from collector.config import app_data_dir
        directory = app_data_dir()
        os.makedirs(directory, exist_ok=True)
        with open(os.path.join(directory, "startup-error.log"), "a",
                  encoding="utf-8") as handle:
            handle.write(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {text}\n")
    except Exception:  # noqa: BLE001 - the message already went to stderr
        pass
    return 3


if __name__ == "__main__":
    sys.exit(main())
