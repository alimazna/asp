"""Standalone MT5 diagnostic for the ASTRA sender.

Run this on the Windows PC when the GUI reports MT5 UNAVAILABLE. It prints the
exact stage that failed and the raw MT5 error, so the cause can be identified
without guessing. It never reads the sender config, never touches the ledger,
and never sends anything to the server.

Usage (from the folder containing the app, in an elevated-free shell):

    "C:\\Program Files\\AstraSender\\AstraSender.exe" --diag-mt5

or, with Python and the MetaTrader5 package installed:

    python diag_mt5.py
"""

from __future__ import annotations

import sys


def _line(label: str, value: object) -> None:
    print(f"{label:<34} {value}")


def main() -> int:
    print("ASTRA Sender - MT5 diagnostic")
    print("=" * 60)

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

    # 2. Can we attach to the running terminal?
    try:
        ok = mt5.initialize()
    except Exception as exc:
        _line("mt5.initialize():", f"EXCEPTION ({exc})")
        return 3
    if not ok:
        err = mt5.last_error()
        _line("mt5.initialize():", "FAILED")
        _line("mt5.last_error():", err)
        print("\nThe Python bridge could not attach to a MetaTrader 5 terminal.")
        print("Check, in this order:")
        print("  1. The MT5 desktop terminal is OPEN (not just installed).")
        print("  2. You are LOGGED IN to an account (the title bar shows a login).")
        print("  3. Tools > Options > Expert Advisors > 'Allow algorithmic trading'.")
        print("  4. Only one MT5 terminal instance is running.")
        print("  5. The terminal was launched normally, not as a different user.")
        print("  6. The terminal has finished its first data download.")
        return 4
    _line("mt5.initialize():", "OK")

    try:
        # 3. Terminal and account identity (no secrets are printed).
        ti = mt5.terminal_info()
        _line("terminal_info():", "OK" if ti else "None")
        if ti:
            _line("  terminal connected:", ti.connected)
            _line("  terminal trade allowed:", ti.trade_allowed)
            _line("  terminal build:", ti.build)
            _line("  terminal company:", ti.company)
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
                visible = info.visible
                _line(f"  {name}:", f"present (visible={visible})")
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
        for tf_name, tf_const in tf_map.items():
            # Position 1 is the most recent CLOSED bar; position 0 is forming.
            rates = mt5.copy_rates_from_pos(symbol, tf_const, 1, 1)
            if rates is None or len(rates) == 0:
                _line(f"  {tf_name}:", f"no data (last_error={mt5.last_error()})")
                continue
            bar = rates[0]
            import datetime as _dt
            ts = _dt.datetime.fromtimestamp(int(bar["time"]), _dt.timezone.utc)
            _line(f"  {tf_name}:", f"closed {ts:%Y-%m-%d %H:%M} UTC  "
                                   f"O={bar['open']} H={bar['high']} "
                                   f"L={bar['low']} C={bar['close']}")
    finally:
        mt5.shutdown()

    print("\nDiagnostic complete. If all rows show a closed candle, MT5 is fine")
    print("and the sender should reach READY on its next retry.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
