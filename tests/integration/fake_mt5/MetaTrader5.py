"""T06 integration double for the MetaTrader5 package.

This is NOT a mock of AURA code: it stands in only for the external, Windows-only
MetaTrader5 dependency so the real bridge (bridge_service.py) and the real
closed-bar/staleness logic can be exercised on Linux without a broker terminal.

Behaviour is driven by environment variables so the driver can pose scenarios:

    FAKE_MT5_AVAILABLE  "0" makes initialize() fail (terminal unavailable)
    FAKE_MT5_TOTAL_BARS number of bars on the series (default 300)
    FAKE_MT5_LAST_TIME  epoch seconds of the newest (forming, index-0) bar;
                        default is "now" so the feed reads FRESH
    FAKE_MT5_SYMBOL     resolved symbol name (default XAUUSD)

Index 0 is the forming bar. copy_rates_from_pos(..., start_pos=1, count) returns
closed bars only, oldest-first, so candles[-1] is the newest *closed* bar.
"""

from __future__ import annotations

import os
import time

# Distinct timeframe constants (values mirror the real package's shape).
TIMEFRAME_M1 = 1
TIMEFRAME_M5 = 5
TIMEFRAME_M15 = 15
TIMEFRAME_M30 = 30
TIMEFRAME_H1 = 16385
TIMEFRAME_H4 = 16388
TIMEFRAME_D1 = 16408
TIMEFRAME_W1 = 32769
TIMEFRAME_MN1 = 49153

_TF_SECONDS = {
    TIMEFRAME_M1: 60,
    TIMEFRAME_M5: 300,
    TIMEFRAME_M15: 900,
    TIMEFRAME_M30: 1800,
    TIMEFRAME_H1: 3600,
    TIMEFRAME_H4: 14400,
    TIMEFRAME_D1: 86400,
    TIMEFRAME_W1: 604800,
    TIMEFRAME_MN1: 2592000,
}

_last_error = (0, "no error")


def _env_int(name: str, default: int) -> int:
    try:
        return int(os.environ.get(name, default))
    except (TypeError, ValueError):
        return default


def last_error():
    return _last_error


def initialize(*args, **kwargs):
    if os.environ.get("FAKE_MT5_AVAILABLE", "1") == "0":
        return False
    return True


def shutdown():
    return None


def version():
    return (5000, 3800, "01 Jan 2026")


class _Account:
    company = "FakeBroker Ltd"
    server = "FakeServer-Demo"


def account_info():
    return _Account()


class _Symbol:
    def __init__(self, name: str) -> None:
        self.name = name


def symbols_get():
    return [_Symbol("EURUSD"), _Symbol("XAUUSD.vx"), _Symbol("XAUUSD")]


def symbol_select(name, enable=True):
    return True


def symbol_info_tick(symbol):
    if os.environ.get("FAKE_MT5_AVAILABLE", "1") == "0":
        return None

    class _Tick:
        time = int(time.time())
        bid = 2000.0
        ask = 2000.5
        last = 2000.25
        volume = 3.0
        flags = 6

    return _Tick()


def symbol_info(symbol):
    class _Info:
        name = symbol
        digits = 2
        point = 0.01
        trade_tick_size = 0.01
        trade_tick_value = 1.0
        trade_contract_size = 100.0
        volume_min = 0.01
        volume_max = 100.0
        volume_step = 0.01
        trade_stops_level = 0
        trade_freeze_level = 0
        trade_mode = 4
        filling_mode = 1
        margin_mode = 2

    return _Info()


def copy_rates_from_pos(symbol, timeframe, start_pos, count):
    """Return `count` bars starting at `start_pos` from the present.

    Oldest-first ordering, index 0 is the forming bar (excluded when
    start_pos >= 1). A list of dicts is returned instead of a numpy array; the
    bridge reads fields by key and treats missing volume columns as zero.
    """
    if os.environ.get("FAKE_MT5_AVAILABLE", "1") == "0":
        return None

    interval = _TF_SECONDS.get(timeframe, 900)
    total = _env_int("FAKE_MT5_TOTAL_BARS", 300)
    last_time = _env_int("FAKE_MT5_LAST_TIME", int(time.time()))
    # Align the newest bar to its timeframe boundary.
    last_time -= last_time % interval

    # Ascending series: index 0 (newest/forming) is the LAST element.
    bars = []
    for i in range(total):
        t = last_time - i * interval
        bars.append({
            "time": t,
            "open": 2000.0 + i,
            "high": 2001.0 + i,
            "low": 1999.0 + i,
            "close": 2000.5 + i,
            "tick_volume": 10 + i,
            "spread": 20,
            "real_volume": 0,
        })
    bars.reverse()  # now ascending, bars[-1] == forming bar at last_time

    if start_pos < 0 or count <= 0:
        return []
    end = len(bars) - start_pos  # exclusive: drop the `start_pos` newest bars
    start = max(0, end - count)
    if end <= 0:
        return []
    return bars[start:end]
