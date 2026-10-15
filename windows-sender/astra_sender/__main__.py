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


VERSION = "0.1.0"


def _say(text: str) -> None:
    """Print without crashing when stdout is None (PyInstaller windowed mode)."""
    if sys.stdout is not None:
        print(text, flush=True)


def main() -> int:
    _module_path_dirs()

    parser = argparse.ArgumentParser(prog="astra_sender",
                                     description="ASTRA closed-candle data sender")
    parser.add_argument("--version", action="store_true",
                        help="print version and exit")
    parser.add_argument("--config", default="",
                        help="path to config.json (default: app data dir)")
    parser.add_argument("--console", action="store_true",
                        help="run headless, print status to stdout")
    args = parser.parse_args()

    if args.version:
        _say(f"AstraSender {VERSION}")
        return 0

    from .config import SenderConfig, default_config_path, app_data_dir
    from .engine import SyncEngine
    from .ledger import DeliveryLedger

    config_path = args.config or default_config_path()
    config = SenderConfig.load(config_path)
    data_dir = os.path.dirname(config_path) or app_data_dir()

    if not config.backend_url or not config.sender_token or not config.sender_id:
        problems = config.validate()
        print("ASTRA Sender is not configured.", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        print(f"Open {config_path} and set backend_url, sender_token and sender_id.",
              file=sys.stderr)
        return 2

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
                               client=client), config, data_dir)

    def poll():
        snapshot = gui._engine.status_snapshot()
        gui.update_from_status(snapshot)

    gui.run(poll)
    return 0


if __name__ == "__main__":
    sys.exit(main())