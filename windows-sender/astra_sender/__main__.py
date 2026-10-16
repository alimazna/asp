"""Astra Sender entry point.

Usage:
    python -m astra_sender [--config PATH] [--console]

--console runs headless (no GUI) with status printed to stdout; useful for
debugging and scheduled runs.
"""

from __future__ import annotations

import argparse
import os
import sys
from typing import Optional

if __package__ in (None, ""):
    # PyInstaller runs this file as a top-level script, where relative imports
    # cannot resolve. Register the package under its real name and re-run this
    # module through the normal import machinery so `from .x import y` works.
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    import astra_sender as _pkg
    from astra_sender.__main__ import main as _main

    if __name__ == "__main__":
        sys.exit(_main())


def _module_path_dirs() -> None:
    """Make the real bridge modules importable when frozen or run from source.

    PyInstaller bundles bridge modules beside this package; the source layout
    keeps them under ../bridge/mt5_python.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(here, "bridge", "mt5_python"),
        os.path.normpath(os.path.join(here, "..", "bridge", "mt5_python")),
        os.path.normpath(os.path.join(here, "..", "..", "bridge", "mt5_python")),
    ]
    for candidate in candidates:
        if os.path.isdir(candidate) and candidate not in sys.path:
            sys.path.insert(0, candidate)


VERSION = "0.4.4"

# Pre-filled into the first-run config template so the user only pastes a token.
DEFAULT_BACKEND_URL = "https://astra-server-production.up.railway.app"

# True when the process has no console (a PyInstaller windowed build). Only
# then does a dialog add information the user cannot already see on stderr.
_HAS_NO_CONSOLE = False


def _is_dialog_enabled(no_console: bool) -> bool:
    """Show message boxes only when there is no console and not disabled.

    A native message box blocks until the user dismisses it, so raising one
    while a console is attached (or in headless CI) would hang the process.
    """
    if not no_console:
        return False
    return os.environ.get("ASTRA_SENDER_NO_DIALOG", "") not in ("1", "true", "yes")


def _say(text: str) -> None:
    """Print without crashing when stdout is None (PyInstaller windowed mode)."""
    if sys.stdout is not None:
        print(text, flush=True)


def _install_console_fallback() -> bool:
    """Capture output and tracebacks when the exe has no console.

    A PyInstaller *windowed* build sets ``sys.stdout``/``sys.stderr`` to None.
    Anything written to them is discarded AND raises inside the print, so a
    startup failure such as a missing config file used to exit silently: the
    user double-clicked and nothing happened with no way to tell why. Route
    both streams to a log next to the config so every startup problem leaves
    evidence on disk.

    Returns True when there was no console (so the caller knows a dialog is
    the only way to reach the user).
    """
    no_console = sys.stdout is None or sys.stderr is None
    if not no_console:
        return False

    try:
        from .config import app_data_dir
        log_path = os.path.join(app_data_dir(), "sender.log")
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        # Line-buffered: the GUI keeps running after these startup messages, so
        # a block-buffered stream would hold them in memory and leave an empty
        # log exactly when the app is hung and the log matters most.
        stream = open(log_path, "a", encoding="utf-8", errors="replace",
                      buffering=1)
    except (ImportError, OSError):
        return True
    if sys.stdout is None:
        sys.stdout = stream
    if sys.stderr is None:
        sys.stderr = stream
    return True


def _show_error_dialog(title: str, message: str) -> None:
    """Best-effort native message box, only when there is no console."""
    if not _is_dialog_enabled(_HAS_NO_CONSOLE):
        return
    try:
        import ctypes
        ctypes.windll.user32.MessageBoxW(None, message, title, 0x40000)  # MB_TOPMOST
    except Exception:
        pass


def main() -> int:
    """Run the app, turning any unexpected crash into a visible message.

    In a windowed exe an uncaught exception terminates with no output, which
    looks identical to "nothing happened". Log the traceback and show a dialog
    so a failure is always diagnosable.
    """
    global _HAS_NO_CONSOLE
    _HAS_NO_CONSOLE = _install_console_fallback()
    try:
        return _main_impl()
    except SystemExit:
        raise
    except BaseException as exc:  # noqa: BLE001 - last-resort guard
        import traceback
        detail = traceback.format_exc()
        print(detail, file=sys.stderr)
        try:
            from .config import app_data_dir
            log_hint = os.path.join(app_data_dir(), "sender.log")
        except Exception:
            log_hint = "sender.log"
        _show_error_dialog(
            "ASTRA Sender — unexpected error",
            f"{type(exc).__name__}: {exc}\n\nA log was written to:\n{log_hint}")
        return 1


def _run_mt5_diagnostic() -> int:
    """Run the MT5 connectivity diagnostic, writing a report beside the log.

    A windowed exe has no console, so the output is also written to
    ``mt5_diag.txt`` in the app data directory where the user can open it.
    """
    import io
    buf = io.StringIO()
    try:
        sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        import diag_mt5  # type: ignore
    except Exception as exc:  # pragma: no cover - packaging fault
        _say(f"diagnostic module missing: {exc}")
        return 1

    real_stdout = sys.stdout
    sys.stdout = buf
    try:
        code = diag_mt5.main()
    except Exception as exc:  # pragma: no cover - defensive
        print(f"diagnostic crashed: {type(exc).__name__}: {exc}")
        code = 1
    finally:
        sys.stdout = real_stdout

    report = buf.getvalue()
    _say(report)
    try:
        # Write beside the config the app actually uses, so the file shows up
        # in the same folder the GUI's "Open config folder" button opens.
        from .config import default_config_path
        path = os.path.join(os.path.dirname(default_config_path()),
                            "mt5_diag.txt")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(report)
        _say(f"\nreport written to: {path}")
    except Exception as exc:
        _say(f"\ncould not write the report: {exc}")
    return code


def _main_impl() -> int:
    _module_path_dirs()

    parser = argparse.ArgumentParser(prog="astra_sender",
                                     description="ASTRA closed-candle data sender")
    parser.add_argument("--version", action="store_true",
                        help="print version and exit")
    parser.add_argument("--selfcheck", action="store_true",
                        help="import every runtime module and exit 0 "
                             "(used by CI to catch frozen-app import errors)")
    parser.add_argument("--config", default="",
                        help="path to config.json (default: app data dir)")
    parser.add_argument("--console", action="store_true",
                        help="run headless, print status to stdout")
    parser.add_argument("--diag-mt5", action="store_true",
                        help="print an MT5 connectivity diagnostic and exit; "
                             "use this when the GUI reports MT5 UNAVAILABLE")
    args = parser.parse_args()

    if args.version:
        _say(f"AstraSender {VERSION}")
        return 0

    if args.diag_mt5:
        return _run_mt5_diagnostic()

    if args.selfcheck:
        # Import the whole runtime surface so a broken/relative import fails the
        # CI smoke test even though the windowed exe can never reach a terminal.
        # The old `--version` check returned before these imports, which is how
        # the relative-import crash shipped in sender-v0.3.0.
        from .config import SenderConfig, default_config_path, app_data_dir
        from .engine import SyncEngine
        from .ledger import DeliveryLedger
        from .collector import ClosedCandleCollector
        from .protocol import ServerClient
        try:
            import tkinter  # noqa: F401
        except ImportError as exc:
            _say(f"GUI modules skipped (tkinter unavailable: {exc})")
        else:
            from .gui import SenderGui  # noqa: F401

        lines = ["ASTRA Sender self-check OK"]
        try:
            import MetaTrader5  # noqa: F401
        except ImportError:
            lines.append("MetaTrader5: NOT bundled")
        else:
            lines.append("MetaTrader5: bundled")
        _say("\n".join(lines))
        # A windowed exe has no stdout, so write the result where CI can read it.
        report = os.path.join(os.getcwd(), "selfcheck.txt")
        try:
            with open(report, "w", encoding="utf-8") as fh:
                fh.write("\n".join(lines) + "\n")
        except OSError:
            pass
        return 0

    from .config import SenderConfig, default_config_path, app_data_dir
    from .engine import SyncEngine
    from .ledger import DeliveryLedger

    config_path = args.config or default_config_path()
    config = SenderConfig.load(config_path)
    data_dir = os.path.dirname(config_path) or app_data_dir()

    needs_config = bool(not config.backend_url or not config.sender_token
                        or not config.sender_id)
    if needs_config:
        # Write an editable template on first run so the user has a file to
        # fill in rather than hand-authoring JSON at an unfamiliar path.
        created = False
        if not os.path.exists(config_path):
            try:
                template = SenderConfig.from_dict(config.to_dict(),
                                                  os.path.dirname(config_path))
                # Pre-fill everything known so the user only pastes the token.
                if not template.backend_url:
                    template.backend_url = DEFAULT_BACKEND_URL
                if not template.sender_id:
                    template.sender_id = "win10-desk-01"
                SenderConfig.save(template, config_path)
                created = True
            except OSError:
                pass

        # Validate what the user actually has on disk now (the template, if we
        # just wrote one) so the reported problems match the file they open.
        reported = SenderConfig.load(config_path).validate() if created else config.validate()
        lines = ["ASTRA Sender is not configured."]
        lines += [f"  - {p}" for p in reported]
        if created:
            lines.append(f"A template was created at:\n  {config_path}")
        lines.append(f"Config file:\n  {config_path}")
        text = "\n".join(lines)
        print(text, file=sys.stderr)

        # Console mode has no widgets, so it can only report and exit. The GUI
        # path instead opens the window with Settings ready, because a windowed
        # exe has no console: the old message box here told the owner what was
        # missing but gave them no way to type the token, so the app looked
        # simply broken when double-clicked.
        #
        # The GUI is only opened in a genuinely interactive windowed session
        # (no console, dialogs allowed). Headless and CI runs set
        # ASTRA_SENDER_NO_DIALOG=1, and there nobody can type a token, so they
        # keep the report-and-exit behaviour.
        interactive = _is_dialog_enabled(_HAS_NO_CONSOLE)
        if args.console or not interactive:
            _show_error_dialog("ASTRA Sender - setup required", text)
            return 2
        try:
            import tkinter  # noqa: F401
        except ImportError:
            _show_error_dialog("ASTRA Sender - setup required", text)
            return 2
        # Recorded before the window is built so CI can prove the interactive
        # first-run path was taken on a runner where a Tk window may not open.
        print("not configured: opening the settings window", file=sys.stderr)

    from .collector import ClosedCandleCollector
    from .protocol import ServerClient

    ledger = DeliveryLedger(config.resolved_ledger_path())
    collector = ClosedCandleCollector(config.symbol)
    client = ServerClient(config.backend_url, config.sender_token,
                          config.sender_id, config.request_timeout_seconds)

    def on_status(snapshot):
        pass

    if args.console:
        engine = SyncEngine(config, collector=collector, ledger=ledger,
                            client=client)
        engine._update_status = lambda **kw: print("status:", kw)  # debug only
        print(f"ASTRA sender (console). backend={config.backend_url} "
              f"symbol={config.symbol}")
        engine.start()
        try:
            while True:
                import time
                time.sleep(5)
        except KeyboardInterrupt:
            engine.stop()
        return 0

    try:
        import tkinter  # noqa: F401  (surface import errors clearly)
    except ImportError as exc:
        print(f"GUI requires tkinter (missing: {exc}); use --console instead.",
              file=sys.stderr)
        return 2

    from .gui import SenderGui
    gui = SenderGui(SyncEngine(config, collector=collector, ledger=ledger,
                               client=client), config, data_dir,
                    config_path=config_path, needs_config=needs_config)

    def poll():
        snapshot = gui._engine.status_snapshot()
        gui.update_from_status(snapshot)

    gui.run(poll)
    return 0


if __name__ == "__main__":
    sys.exit(main())