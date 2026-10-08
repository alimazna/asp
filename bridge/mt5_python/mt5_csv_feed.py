"""T13 evidential - replay a committed M1 CSV as an MT5-compatible feed.

This is *real* engine code (not a test double): given a canonical OHLCV CSV it
synthesises the bars a live MetaTrader5 terminal would serve, with the same
closed-bar contract the bridge relies on - ascending time, index 0 is the
forming (newest, still-open) bar, ``copy_rates_from_pos(start_pos=1)`` returns
closed bars only.

It is used by two callers:

* the packaged bridge's ``MetaTrader5`` shim (``tests/integration/fake_mt5``),
  so the *real* ``bridge_service.py`` and the *real* ``aura_backend_host`` can
  be exercised end-to-end over real gold data with no terminal and no network;
* tests that want the same bars directly.

The CSV is read-only input. Higher timeframes are aggregated from the base M1
bars by timeframe-boundary bucketing; the final (possibly partial) bucket is the
forming bar and is excluded for any ``start_pos >= 1`` request, so decision bars
are always closed.

Timestamps in the corpus carry no timezone label (broker server time). They are
parsed as written and converted to epoch seconds with ``timegm``; ordering is
preserved, which is all causality requires.

Time alignment
--------------
The corpus ends at a fixed historical instant, so replayed bars would look
future-dated to a host running at real wall-clock. Unless ``FAKE_MT5_CSV_NO_SHIFT``
is set, the whole series is translated by one constant offset so its newest M1
bar lands on the current minute, turning the corpus into the trailing window a
live terminal would serve. This is a uniform translation: relative order, bar
geometry and closed-bar semantics are unchanged, and no future bar is ever
offered as a decision bar. ``FAKE_MT5_REPLAY_NOW`` (epoch seconds) pins the
offset for deterministic runs.
"""

from __future__ import annotations

import calendar
import csv
import os
import time
from typing import Dict, List, Optional

# MT5 timeframe constants (values mirror the real package's shape).
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

_DEFAULT_SYMBOL = "XAUUSD"


def _parse_epoch(value: str) -> int:
    """Parse 'YYYY-MM-DD HH:MM:SS' (or ISO 'T') as UTC epoch seconds."""
    text = value.strip().replace("T", " ")
    if text.endswith("Z"):
        text = text[:-1]
    try:
        return int(calendar.timegm(time.strptime(text, "%Y-%m-%d %H:%M:%S")))
    except ValueError:
        return int(calendar.timegm(time.strptime(text, "%Y-%m-%d %H:%M")))


def _replay_offset(newest_m1_epoch: int) -> int:
    """Constant shift so the corpus ends on the current minute (see docstring)."""
    override = os.environ.get("FAKE_MT5_REPLAY_NOW", "").strip()
    try:
        now = int(override) if override else int(time.time())
    except ValueError:
        now = int(time.time())
    target = now - (now % 60)
    return target - newest_m1_epoch


class CsvFeed:
    """An MT5-like bar source backed by one canonical M1 CSV."""

    def __init__(self, csv_path: str, symbol: str = _DEFAULT_SYMBOL) -> None:
        self.csv_path = csv_path
        self.symbol = symbol
        self.shift_seconds = 0
        self._m1: List[dict] = []
        self._cache: Dict[int, List[dict]] = {}
        self._load()

    def _load(self) -> None:
        rows: List[dict] = []
        with open(self.csv_path, "r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            for raw in reader:
                try:
                    t = _parse_epoch(raw["timestamp"])
                    rows.append({
                        "time": t,
                        "open": float(raw["open"]),
                        "high": float(raw["high"]),
                        "low": float(raw["low"]),
                        "close": float(raw["close"]),
                        "tick_volume": int(float(raw.get("volume") or 0)),
                        "spread": 0,
                        "real_volume": 0,
                    })
                except (KeyError, TypeError, ValueError):
                    continue
        rows.sort(key=lambda r: r["time"])
        if not os.environ.get("FAKE_MT5_CSV_NO_SHIFT") and rows:
            offset = _replay_offset(rows[-1]["time"])
            self.shift_seconds = offset
            for row in rows:
                row["time"] += offset
        self._m1 = rows

    def total_bars(self) -> int:
        return len(self._m1)

    def _aggregate(self, interval: int) -> List[dict]:
        if interval in self._cache:
            return self._cache[interval]
        buckets: Dict[int, dict] = {}
        order: List[int] = []
        for row in self._m1:
            bucket = row["time"] - (row["time"] % interval)
            bar = buckets.get(bucket)
            if bar is None:
                buckets[bucket] = {
                    "time": bucket,
                    "open": row["open"],
                    "high": row["high"],
                    "low": row["low"],
                    "close": row["close"],
                    "tick_volume": row["tick_volume"],
                    "spread": row["spread"],
                    "real_volume": row["real_volume"],
                }
                order.append(bucket)
            else:
                bar["high"] = max(bar["high"], row["high"])
                bar["low"] = min(bar["low"], row["low"])
                bar["close"] = row["close"]
                bar["tick_volume"] += row["tick_volume"]
        series = [buckets[b] for b in order]
        series.sort(key=lambda b: b["time"])
        self._cache[interval] = series
        return series

    def copy_rates_from_pos(self, symbol: str, timeframe: int, start_pos: int,
                            count: int) -> List[dict]:
        """MT5-shaped window: ascending, index 0 is the forming bar."""
        if start_pos < 0 or count <= 0:
            return []
        interval = _TF_SECONDS.get(timeframe)
        if interval is None:
            return []
        series = self._aggregate(interval)
        end = len(series) - start_pos  # drop the `start_pos` newest bars
        if end <= 0:
            return []
        start = max(0, end - count)
        return [dict(bar) for bar in series[start:end]]

    def newest_closed_time(self, timeframe: int) -> Optional[int]:
        interval = _TF_SECONDS.get(timeframe)
        if interval is None:
            return None
        series = self._aggregate(interval)
        if len(series) < 2:
            return None
        return series[-2]["time"]

    def last_close(self) -> float:
        return self._m1[-1]["close"] if self._m1 else 0.0


_feeds: Dict[str, CsvFeed] = {}


def feed_for(csv_path: str, symbol: str = _DEFAULT_SYMBOL) -> CsvFeed:
    """Return a cached feed for ``csv_path`` (one parse per file per process)."""
    key = os.path.abspath(csv_path)
    feed = _feeds.get(key)
    if feed is None:
        feed = CsvFeed(key, symbol=symbol)
        _feeds[key] = feed
    return feed


def csv_path_from_env() -> str:
    return os.environ.get("FAKE_MT5_CSV", "").strip()


def symbol_from_env() -> str:
    return os.environ.get("FAKE_MT5_CSV_SYMBOL", _DEFAULT_SYMBOL).strip() or _DEFAULT_SYMBOL
