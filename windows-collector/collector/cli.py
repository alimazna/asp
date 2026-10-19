"""Command-line entry point.

Runs the collector either as a one-shot pass (useful for verification and for a
scheduled task) or as a continuous loop, and prints a status view built only
from real counters.

Secrets never appear on the command line: the token is read from its own file
(``config.save_token``) or from the ``ASTRA_COLLECTOR_TOKEN`` environment
variable, which keeps it out of shell history and process listings.
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import sys
import time
from typing import Optional

from . import __version__
from .config import (
    CollectorConfig,
    default_config_path,
    default_ledger_path,
    default_store_path,
    default_token_path,
    load_token,
    save_token,
)
from .engine import SyncEngine
from .ledger import DeliveryLedger
from .mt5_reader import Mt5Reader
from .receiver import ReceiverClient, ReceiverError
from .store import CandleStore

TOKEN_ENV = "ASTRA_COLLECTOR_TOKEN"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="astra-collector",
        description="Collect closed MetaTrader 5 candles and deliver them to a "
                    "configured receiving endpoint, exactly once logically.")
    parser.add_argument("--version", action="version",
                        version=f"astra-collector {__version__}")
    parser.add_argument("--config", default="",
                        help="path to config.json (default: app data dir)")
    parser.add_argument("--destination", default="",
                        help="override the receiving endpoint URL")
    parser.add_argument("--symbol", default="",
                        help="override the symbol to read")
    parser.add_argument("--once", action="store_true",
                        help="run a single pass, print the result, and exit")
    parser.add_argument("--status", action="store_true",
                        help="print the current status and exit")
    parser.add_argument("--show-config", action="store_true",
                        help="print the effective configuration (no token) and exit")
    parser.add_argument("--set-destination", default="",
                        help="save a new receiving endpoint URL and exit")
    parser.add_argument("--set-token", action="store_true",
                        help="read a new token from stdin, save it, and exit")
    parser.add_argument("--set-symbol", default="",
                        help="save a new symbol and exit")
    parser.add_argument("--set-sender-id", default="",
                        help="save a new sender id and exit")
    parser.add_argument("--allow-insecure-http", action="store_true",
                        help="permit a plain-HTTP destination (local dev only)")
    parser.add_argument("--json", action="store_true",
                        help="machine-readable output")
    parser.add_argument("--interval", type=int, default=0,
                        help="override the poll interval in seconds")
    return parser


def _print(text: str, as_json: bool, payload=None) -> None:
    if as_json and payload is not None:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print(text)


def format_status(status: dict) -> str:
    """Human-readable status. Every number shown is a real counter."""
    lines = [
        f"Destination      : {status.get('destination') or '-'}",
        f"Receiver state   : {status.get('receiver_state')}",
        f"MT5 state        : {status.get('mt5_state')}",
        f"Resolved symbol  : {status.get('resolved_symbol') or '-'}",
        f"Broker           : {status.get('broker') or '-'}",
        f"Clock offset     : {status.get('clock_offset_seconds', 0)}s",
        f"Last sync        : "
        f"{_format_time(status.get('last_sync_at', 0))}",
        "",
        "Counters (this process):",
        f"  sent           : {status.get('sent_total', 0)}",
        f"  acknowledged   : {status.get('acknowledged_total', 0)}",
        f"  deduplicated   : {status.get('deduplicated_total', 0)}",
        f"  queued         : {status.get('queued_total', 0)}",
        f"  failed         : {status.get('failed_total', 0)}",
        f"  rejected       : {status.get('rejected_total', 0)}",
        "",
        "Per timeframe (last read / last acknowledged / stored / gaps):",
    ]
    reads = status.get("last_read_per_timeframe", {})
    acked = status.get("last_acked_per_timeframe", {})
    stored = status.get("stored_count_per_timeframe", {})
    gaps = status.get("gap_count_per_timeframe", {})
    timeframes = ["M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"]
    for timeframe in timeframes:
        read = reads.get(timeframe)
        ack = acked.get(timeframe)
        read_text = _format_time(read.get("open_time_utc")) if read else "-"
        ack_text = _format_time(ack.get("open_time_utc")) if ack else "-"
        lines.append(
            f"  {timeframe:<4} read {read_text:<20} acked {ack_text:<20} "
            f"stored {stored.get(timeframe, 0):<6} gaps {gaps.get(timeframe, 0)}")
    if status.get("last_error"):
        lines.extend(["", f"Last error       : {status['last_error']}",
                      f"Error code       : {status.get('last_error_code', '')}"])
    notes = status.get("notes") or status.get("local_gaps") or []
    if notes:
        lines.append("")
        lines.append("Notes:")
        for note in notes[:10]:
            lines.append(f"  - {note}")
    return "\n".join(lines)


def _format_time(epoch: Optional[int]) -> str:
    if not epoch:
        return "-"
    return time.strftime("%Y-%m-%d %H:%M:%SZ", time.gmtime(int(epoch)))


def _load_config(args) -> CollectorConfig:
    path = args.config or default_config_path()
    config = CollectorConfig.load(path)
    if args.destination:
        config.backend_url = args.destination.strip()
    if args.symbol:
        config.symbol = args.symbol.strip()
    if args.allow_insecure_http:
        config.allow_insecure_http = True
    if args.interval:
        config.poll_interval_seconds = int(args.interval)
    return config


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    config_path = args.config or default_config_path()
    config = _load_config(args)

    # -- configuration edits ------------------------------------------------
    if args.set_destination or args.set_symbol or args.set_sender_id:
        if args.set_destination:
            config.backend_url = args.set_destination.strip()
        if args.set_symbol:
            config.symbol = args.set_symbol.strip()
        if args.set_sender_id:
            config.sender_id = args.set_sender_id.strip()
        config.save(config_path)
        print(f"saved configuration to {config_path}")
        return 0

    if args.set_token:
        token = sys.stdin.read().strip()
        if not token:
            print("no token supplied on stdin", file=sys.stderr)
            return 2
        save_token(token)
        print(f"saved token to {default_token_path()}")
        return 0

    if args.show_config:
        payload = config.to_dict()
        payload["token_configured"] = bool(load_token())
        payload["config_path"] = config_path
        payload["token_path"] = default_token_path()
        payload["store_path"] = default_store_path()
        payload["ledger_path"] = default_ledger_path()
        _print(json.dumps(payload, indent=2, sort_keys=True), args.json, payload)
        return 0

    token = load_token() or os.environ.get(TOKEN_ENV, "")
    problems = config.validate()
    if not token:
        problems.append(
            f"No token configured. Save one to {default_token_path()} or set "
            f"{TOKEN_ENV}.")

    store = CandleStore(default_store_path())
    ledger = DeliveryLedger(default_ledger_path())

    if args.status:
        client = ReceiverClient(config.backend_url, token,
                                timeout_seconds=config.request_timeout_seconds)
        engine = SyncEngine(config, store, ledger, client,
                            reader=Mt5Reader(symbol=config.symbol))
        engine.refresh_ledger_counters()
        status = engine.status()
        status["destination"] = ledger.destination_id(config.backend_url)
        status["config_problems"] = problems
        _print(format_status(status), args.json, status)
        store.close()
        ledger.close()
        return 0

    if problems:
        print("Configuration is not usable:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        print(f"\nConfig file: {config_path}", file=sys.stderr)
        print(f"Token file : {default_token_path()}", file=sys.stderr)
        store.close()
        ledger.close()
        return 2

    client = ReceiverClient(config.backend_url, token,
                            timeout_seconds=config.request_timeout_seconds)
    engine = SyncEngine(config, store, ledger, client,
                        reader=Mt5Reader(symbol=config.symbol))
    engine.refresh_ledger_counters()

    if args.once:
        try:
            plan = engine.sync_once()
        except (ReceiverError, Exception) as exc:  # noqa: BLE001
            status = engine.status()
            status["config_problems"] = []
            print(format_status(status))
            print(f"\npass failed: {getattr(exc, 'message', exc)}",
                  file=sys.stderr)
            store.close()
            ledger.close()
            return 1
        summary = {
            "sent": plan.send_count,
            "gaps": len(plan.gaps_to_fill),
            "notes": plan.notes,
            "status": engine.status(),
        }
        _print(format_status(engine.status()), args.json, summary)
        store.close()
        ledger.close()
        return 0

    # -- continuous loop ----------------------------------------------------
    stopping = {"flag": False}

    def _handle_signal(signum, frame):  # noqa: ARG001
        stopping["flag"] = True
        print("\nstopping: finishing the current pass and closing the ledger...",
              file=sys.stderr)
        engine.stop()

    for name in ("SIGINT", "SIGTERM"):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), _handle_signal)

    print(f"astra-collector {__version__}")
    print(f"destination: {ledger.destination_id(config.backend_url)}")
    print(f"symbol: {config.symbol}  timeframes: {', '.join(config.timeframes)}")
    print(f"store: {default_store_path()}")
    print(f"ledger: {default_ledger_path()}")
    print("press Ctrl+C to stop\n")
    engine.start()
    try:
        while not stopping["flag"]:
            time.sleep(1.0)
    except KeyboardInterrupt:
        engine.stop()
    finally:
        engine.stop()
        store.close()
        ledger.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
