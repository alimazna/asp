"""Standalone MT5 diagnostic for the ASTRA sender.

Run this on the Windows PC when the GUI reports MT5 UNAVAILABLE. It prints the
exact stage that failed and the raw MT5 error, so the cause can be identified
without guessing. It never reads the sender config, never touches the ledger,
and never sends anything to the server.

Usage (from the folder containing the app):

    "C:\\Program Files\\AstraSender\\AstraSender.exe" --diag-mt5

or, with Python and the MetaTrader5 package installed:

    python diag_mt5.py

Exit codes:
    0  every stage passed
    2  the MetaTrader5 package is not importable
    3  mt5.initialize() raised
    4  mt5.initialize() returned False, or timed out (terminal not reachable)
    5  no gold symbol is available from the broker
    6  the terminal is reachable but returns no usable closed candles
"""

from __future__ import annotations

import os
import sys
import threading
import time

# The IPC handshake is bounded. MT5's own default is 60000 ms, and an
# unbounded attach is why an unreachable terminal looked like a hang instead of
# a reported error. This is the diagnostic's own ceiling, independent of the
# sender's configured timeout, so the tool always finishes.
_INIT_TIMEOUT_MS = 15000
_INIT_WALL_CLOCK_GRACE_SECONDS = 30


def _line(label: str, value: object) -> None:
    print(f"{label:<34} {value}")


def _is_admin() -> bool:
    """True when this process runs elevated (Windows only)."""
    if os.name != "nt":
        return False
    try:
        import ctypes
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def _terminal_elevated_hint() -> str:
    """Explain the privilege-level rule for the MT5 IPC handshake.

    MT5 exposes no "is the terminal elevated" flag, so we report our own level
    and state the rule the user must satisfy: this program and terminal64.exe
    must run at the SAME level. If one is elevated and the other is not, the
    handshake fails (typically -10005 IPC timeout) while the terminal still
    looks perfectly healthy.
    """
    ours = "ADMINISTRATOR (elevated)" if _is_admin() else "normal (not elevated)"
    return (
        "\nPRIVILEGE LEVEL CHECK (the most common cause of this failure)\n"
        f"  This diagnostic is running as: {ours}\n"
        "  MetaTrader 5 must run at the SAME level.\n"
        "  If this says 'normal', open MetaTrader 5 normally (NOT as Administrator).\n"
        "  If this says 'ADMINISTRATOR', open MetaTrader 5 as Administrator too.\n"
        "  A mismatch fails the handshake while the terminal still looks fine.\n"
    )


def _run_with_deadline(func, timeout_ms: int):
    """Run func() on a worker thread; return (done, outcome, elapsed_seconds).

    MT5's IPC call can block for its full timeout, and a wedged terminal can
    block longer than requested. The worker is a daemon so a stuck call cannot
    keep the process alive: the diagnostic reports the timeout and exits
    instead of hanging forever.
    """
    box = {}

    def target():
        try:
            box["result"] = func()
        except BaseException as exc:  # noqa: BLE001 - reported, never swallowed
            box["error"] = exc

    started = time.time()
    thread = threading.Thread(target=target, daemon=True)
    thread.start()
    deadline = time.time() + (timeout_ms / 1000.0) + _INIT_WALL_CLOCK_GRACE_SECONDS
    while time.time() < deadline:
        if not thread.is_alive():
            break
        time.sleep(0.05)
    elapsed = time.time() - started
    if thread.is_alive():
        return False, None, elapsed
    if "error" in box:
        return True, box["error"], elapsed
    return True, box.get("result"), elapsed


def _report_last_error(err: object) -> None:
    """Explain the raw MT5 error code, which is the key support detail."""
    code = None
    if isinstance(err, (tuple, list)) and err:
        code = err[0]
    elif isinstance(err, int):
        code = err
    explanations = {
        -10005: ("IPC timeout. The terminal did not answer the handshake. Almost "
                 "always a privilege mismatch between this program and "
                 "terminal64.exe, or the terminal is not actually running."),
        -10004: ("IPC init failed. The terminal could not be started or attached "
                 "to. Check the terminal path and that one instance is running."),
        -10003: ("IPC send failed. The terminal closed the connection; restart "
                 "MetaTrader 5."),
        -10001: ("IPC create failed. A firewall or antivirus may be blocking the "
                 "local IPC pipe."),
        -6: ("Terminal is not authorized. Log in to your account in the "
             "terminal, then re-run."),
        -2: ("Invalid parameters, often a wrong terminal path."),
    }
    if code is None:
        return
    print(f"\nError code {code}:")
    note = explanations.get(code)
    if note:
        print(f"  {note}")
    else:
        print("  Not a code this diagnostic recognizes. Report it verbatim.")


def main() -> int:
    print("ASTRA Sender - MT5 diagnostic")
    print("=" * 60)
    _line("running elevated:", "YES" if _is_admin() else "no")
    _line("python:", sys.version.split()[0])

    # 1. Is the MetaTrader5 package importable at all?
    try:
        import MetaTrader5 as mt5  # noqa: N813
    except Exception as exc:
        _line("MetaTrader5 import:", f"FAILED ({exc})")
        print("\nThe MetaTrader5 package is not available to this interpreter.")
        print("If you are running this from source, run: pip install MetaTrader5")
        print("If you are running the installed app, reinstall it.")
        return 2
    _line("MetaTrader5 import:", "OK")
    _line("MetaTrader5 version:", getattr(mt5, "__version__", "unknown"))

    # 2. Can we attach to the running terminal? Bounded so it cannot hang.
    print(f"\nAttaching to the terminal (timeout {_INIT_TIMEOUT_MS} ms)...")
    done, outcome, elapsed = _run_with_deadline(
        lambda: mt5.initialize(timeout=_INIT_TIMEOUT_MS), _INIT_TIMEOUT_MS)

    if not done:
        _line("mt5.initialize():", f"TIMED OUT after {elapsed:.1f}s")
        print("\nThe MT5 IPC handshake did not complete. This is NOT a slow PC;")
        print("it means the terminal never answered. Most likely causes:")
        print(_terminal_elevated_hint())
        print("  1. No MetaTrader 5 terminal is running.")
        print("  2. More than one MT5 terminal is installed or running, and the")
        print("     wrong one (or none) is the attach target.")
        print("  3. A broker-specific terminal is installed under a suffixed")
        print("     folder; set its full path to terminal64.exe in ASTRA Sender")
        print("     Settings and re-run this diagnostic.")
        print("  4. The terminal is still starting or finishing its first")
        print("     history download. Wait for it to settle, then retry.")
        return 4

    if isinstance(outcome, BaseException):
        _line("mt5.initialize():", f"EXCEPTION ({type(outcome).__name__}: {outcome})")
        print(_terminal_elevated_hint())
        return 3

    if not outcome:
        try:
            err = mt5.last_error()
        except Exception:
            err = "unavailable"
        _line("mt5.initialize():", f"FAILED after {elapsed:.1f}s")
        # The raw code is the single most useful line for remote support.
        _line("mt5.last_error():", err)
        _report_last_error(err)
        print(_terminal_elevated_hint())
        print("Other checks, in order:")
        print("  1. The MT5 desktop terminal is OPEN (not just installed).")
        print("  2. You are LOGGED IN to an account (the title bar shows a login).")
        print("  3. Tools > Options > Expert Advisors > 'Allow algorithmic trading'.")
        print("  4. Only one MT5 terminal instance is running.")
        print("  5. The terminal has finished its first data download.")
        return 4

    _line("mt5.initialize():", f"OK ({elapsed:.1f}s)")

    try:
        # 3. Terminal and account identity (no secrets are printed).
        ti = mt5.terminal_info()
        _line("terminal_info():", "OK" if ti else "None")
        if ti:
            _line("  terminal connected:", ti.connected)
            _line("  terminal trade allowed:", ti.trade_allowed)
            _line("  terminal build:", ti.build)
            _line("  terminal company:", ti.company)
            _line("  terminal path:", getattr(ti, "path", "unknown"))
        ai = mt5.account_info()
        _line("account_info():", "OK" if ai else "None (not logged in?)")
        if ai:
            _line("  account login:", ai.login)
            _line("  account server:", ai.server)

        # 4. Which gold symbol does this broker expose?
        print("\nSymbol discovery for gold:")
        found = []
        for name in ("XAUUSD", "GOLD", "XAUUSDm", "XAUUSD.a", "XAUUSD.pro"):
            info = mt5.symbol_info(name)
            if info is not None:
                found.append(name)
                _line(f"  {name}:", f"present (visible={info.visible})")
        if not found:
            candidates = mt5.symbols_get("*XAU*") or []
            names = [s.name for s in candidates][:20]
            _line("  XAU* matches:", names or "none")
            if not names:
                print("\nNo gold symbol is available from this broker/server.")
                print("Open Market Watch in MT5 and add your broker's gold symbol.")
                return 5
            found = names

        # 5. Read one closed candle per timeframe and show its timestamp.
        print("\nLatest closed candle per timeframe:")
        tf_map = {
            "M1": mt5.TIMEFRAME_M1, "M5": mt5.TIMEFRAME_M5,
            "M15": mt5.TIMEFRAME_M15, "M30": mt5.TIMEFRAME_M30,
            "H1": mt5.TIMEFRAME_H1, "H4": mt5.TIMEFRAME_H4,
            "D1": mt5.TIMEFRAME_D1, "W1": mt5.TIMEFRAME_W1,
            "MN1": mt5.TIMEFRAME_MN1,
        }
        symbol = found[0]
        _line("using symbol:", symbol)
        # Make sure the symbol is selected so rates are available.
        mt5.symbol_select(symbol, True)
        missing = 0
        for tf_name, tf_const in tf_map.items():
            # Position 1 is the most recent CLOSED bar; position 0 is forming.
            rates = mt5.copy_rates_from_pos(symbol, tf_const, 1, 1)
            if rates is None or len(rates) == 0:
                missing += 1
                _line(f"  {tf_name}:", f"no data (last_error={mt5.last_error()})")
                continue
            bar = rates[0]
            import datetime as _dt
            ts = _dt.datetime.fromtimestamp(int(bar["time"]), _dt.timezone.utc)
            _line(f"  {tf_name}:", f"closed {ts:%Y-%m-%d %H:%M} UTC  "
                                   f"O={bar['open']} H={bar['high']} "
                                   f"L={bar['low']} C={bar['close']}")
        if missing == len(tf_map):
            print("\nThe terminal answered but returned no candles at all.")
            print("Let the terminal finish downloading history, then re-run.")
            return 6
    finally:
        try:
            mt5.shutdown()
        except Exception:
            pass

    print("\nDiagnostic complete. If all rows show a closed candle, MT5 is fine")
    print("and the sender should reach READY on its next retry.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
