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

import os
import sys


def _write(text: str, stream: str = "stdout") -> None:
    """Write without crashing when the stream is None.

    A PyInstaller *windowed* build sets ``sys.stdout`` and ``sys.stderr`` to
    None. Writing to None raises AttributeError, and an unhandled exception in
    a windowed exe pops a native message box that waits for a click forever -
    which is how a CI smoke run of `--version` hung instead of printing.
    """
    target = sys.stdout if stream == "stdout" else sys.stderr
    if target is None:
        return
    try:
        target.write(text)
        target.flush()
    except (OSError, ValueError, AttributeError):
        pass


def _dialogs_suppressed() -> bool:
    """True when a blocking message box must never be shown.

    Set `ASTRA_COLLECTOR_NO_DIALOG=1` in headless runs. A native message box
    waits for a click that never comes in CI, hanging the job until it times
    out.
    """
    return os.environ.get("ASTRA_COLLECTOR_NO_DIALOG", "").lower() in (
        "1", "true", "yes")


def _install_console_fallback() -> bool:
    """Give a windowed build somewhere to write, and report whether it had none.

    A PyInstaller *windowed* build sets ``sys.stdout`` and ``sys.stderr`` to
    None. Two things then go wrong: anything printed is discarded, and an
    unhandled exception pops a native message box that waits for a click that
    never comes. A CI smoke run has nobody to click it, so it hangs until the
    job times out. Route both streams to a log next to the configuration so
    every startup problem leaves readable evidence instead.

    Returns True when there was no console.
    """
    no_console = sys.stdout is None or sys.stderr is None
    if not no_console:
        return False
    try:
        from collector.config import app_data_dir
        directory = app_data_dir()
        os.makedirs(directory, exist_ok=True)
        handle = open(os.path.join(directory, "console.log"), "a",
                      encoding="utf-8", buffering=1)
    except Exception:  # noqa: BLE001 - a missing log must not stop startup
        handle = open(os.devnull, "w", encoding="utf-8")
    if sys.stdout is None:
        sys.stdout = handle
    if sys.stderr is None:
        sys.stderr = handle
    return True


def main(argv=None) -> int:
    _install_console_fallback()
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
    """Import every module the frozen app needs at runtime.

    The result is written to `selfcheck.txt` in the data directory as well as
    stderr, because a windowed build has no console: without the file, a
    failure in CI or on the owner's machine leaves nothing to read. This
    mirrors the proven pattern in windows-sender.
    """
    modules = [
        "collector.cli", "collector.config", "collector.display",
        "collector.engine", "collector.gui", "collector.identity",
        "collector.ledger", "collector.mt5_reader", "collector.planner",
        "collector.receiver", "collector.store",
    ]
    lines = []
    for name in modules:
        try:
            __import__(name)
        except Exception as exc:  # noqa: BLE001 - report, do not mask
            lines.append(f"FAIL {name}: {type(exc).__name__}: {exc}")
    try:
        import tkinter  # noqa: F401
        lines.append("ok   tkinter")
    except Exception as exc:  # noqa: BLE001
        lines.append(f"FAIL tkinter: {type(exc).__name__}: {exc}")
    # MetaTrader5 is only present on Windows and is imported lazily, so its
    # absence is not a packaging failure on any other host. It is reported but
    # never imported here on a non-Windows build.
    if sys.platform == "win32":
        try:
            import MetaTrader5  # noqa: F401
            lines.append("ok   MetaTrader5 (bundled)")
        except Exception as exc:  # noqa: BLE001
            lines.append(f"FAIL MetaTrader5: {type(exc).__name__}: {exc}")

    failures = [line for line in lines if line.startswith("FAIL")]
    status = "selfcheck: all runtime modules import cleanly"
    if failures:
        status = "selfcheck failed"

    report = "\n".join([status] + lines) + "\n"
    sys.stderr.write(report)
    _write_report(report)
    return 4 if failures else 0


def _write_report(text: str) -> None:
    """Write a diagnostic report somewhere CI and the owner can both read it."""
    import os
    try:
        from collector.config import app_data_dir
        directory = app_data_dir()
        os.makedirs(directory, exist_ok=True)
        target = os.path.join(directory, "selfcheck.txt")
    except Exception:  # noqa: BLE001 - fall back to the working directory
        target = "selfcheck.txt"
    try:
        with open(target, "w", encoding="utf-8") as handle:
            handle.write(text)
    except OSError:
        pass


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
    # A windowed exe turns an unhandled exception into a native message box
    # that waits for a click. In a headless run there is nobody to click it,
    # so the process hangs until the job times out. When the caller has asked
    # for no dialog, convert any escaping exception into a logged, non-zero
    # exit instead.
    try:
        _code = main()
    except SystemExit:
        raise
    except BaseException as _exc:  # noqa: BLE001 - last resort, never a dialog
        import traceback
        _write(f"unhandled {type(_exc).__name__}: {_exc}\n"
               + traceback.format_exc(), stream="stderr")
        try:
            _write_report(f"unhandled {type(_exc).__name__}: {_exc}\n"
                          + traceback.format_exc())
        except Exception:  # noqa: BLE001
            pass
        if not _dialogs_suppressed():
            raise
        _code = 5
    sys.exit(_code)
