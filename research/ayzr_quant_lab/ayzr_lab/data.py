"""Corpus loading and multi-timeframe construction.

Reads the immutable Dukascopy M1 BID/ASK gzip corpus and builds
calendar-aligned higher timeframes. Two invariants matter for honest research:

1. Only CLOSED bars are ever handed to a decision. A bar for instant ``t`` is
   usable only once its own interval has finished, so ``resample`` never emits a
   partial bucket as if it were complete.
2. Alignment is calendar/UTC based (a bucket starts at the boundary and carries
   the first open, max high, min low, last close, summed volume of its members),
   matching the aggregation the project already uses in
   ``research/features_real/run_features.py``.

The raw corpus is read-only; nothing here rewrites the source files.
"""

from __future__ import annotations

import gzip
import os
from dataclasses import dataclass
from typing import Dict, Iterator, List, Optional, Sequence, Tuple

from .astra_port import Bar, TIMEFRAME_SECONDS, Timeframe

CORPUS_ROOT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "data", "xauusd_m1",
)


def corpus_path(year: int, side: str = "bid") -> str:
    base = os.path.join(CORPUS_ROOT, "ask" if side == "ask" else "")
    return os.path.join(base, f"{year}.csv.gz")


def load_m1(year: int, side: str = "bid") -> List[Bar]:
    """Load one year of M1 bars (BID or ASK) as a list of ``Bar``."""
    path = corpus_path(year, side)
    bars: List[Bar] = []
    with gzip.open(path, "rt") as fh:
        header = fh.readline()  # timestamp,open,high,low,close[,volume]
        for line in fh:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) < 5:
                continue
            ts = int(parts[0]) // 1000  # ms -> s
            vol = float(parts[5]) if len(parts) > 5 else 0.0
            bars.append(Bar(ts, float(parts[1]), float(parts[2]),
                            float(parts[3]), float(parts[4]), vol, Timeframe.M1))
    return bars


def load_m1_range(start_year: int, end_year: int, side: str = "bid") -> List[Bar]:
    out: List[Bar] = []
    for y in range(start_year, end_year + 1):
        out.extend(load_m1(y, side))
    return out


def align_grid(bars: Sequence[Bar], tf: Timeframe) -> List[Bar]:
    """Aggregate M1 bars to ``tf`` on a UTC calendar grid.

    A bucket is emitted only once a later bucket has started, so a possibly
    still-forming trailing bucket is never returned as if it were closed. This
    is what makes the series lookahead-free: no partial bar can reach a decision.
    """
    step = TIMEFRAME_SECONDS[tf]
    if tf == Timeframe.M1:
        return list(bars)

    out: List[Bar] = []
    cur_bucket: Optional[int] = None
    o = h = l = c = 0.0
    vol = 0.0

    for b in bars:
        bucket = (b.open_time_sec // step) * step
        if cur_bucket is None:
            cur_bucket = bucket
            o, h, l, c = b.open, b.high, b.low, b.close
            vol = b.volume
        elif bucket != cur_bucket:
            # The previous bucket is now known to be closed.
            out.append(Bar(cur_bucket, o, h, l, c, vol, tf))
            cur_bucket = bucket
            o, h, l, c = b.open, b.high, b.low, b.close
            vol = b.volume
        else:
            h = max(h, b.high)
            l = min(l, b.low)
            c = b.close
            vol += b.volume
    # The trailing bucket is intentionally NOT emitted: it may still be forming.
    return out


@dataclass
class Series:
    """A timeframe's closed bars plus an index for as-of lookups."""
    timeframe: Timeframe
    bars: List[Bar]

    def __len__(self) -> int:
        return len(self.bars)

    def window_ending_at(self, open_time_sec: int, count: int) -> List[Bar]:
        """The last ``count`` bars whose open time is <= ``open_time_sec``."""
        lo, hi = 0, len(self.bars)
        while lo < hi:
            mid = (lo + hi) // 2
            if self.bars[mid].open_time_sec <= open_time_sec:
                lo = mid + 1
            else:
                hi = mid
        end = lo
        start = max(0, end - count)
        return self.bars[start:end]


def build_series(bars_m1: Sequence[Bar], timeframes: Sequence[Timeframe]) -> Dict[Timeframe, Series]:
    return {tf: Series(tf, align_grid(bars_m1, tf)) for tf in timeframes}


def build_bid_ask_m1(year: int) -> Tuple[List[Bar], List[Bar]]:
    """BID and ASK M1, restricted to the timestamps present in BOTH sides.

    The two feeds are not row-aligned (ASK carries extra minutes), so a spread is
    only defined on the intersection. Returns (bid, ask) sharing one time axis.
    """
    bid = load_m1(year, "bid")
    ask = load_m1(year, "ask")
    ask_map = {b.open_time_sec: b for b in ask}
    b2: List[Bar] = []
    a2: List[Bar] = []
    for b in bid:
        a = ask_map.get(b.open_time_sec)
        if a is not None:
            b2.append(b)
            a2.append(a)
    return b2, a2


def spread_series(year: int) -> List[Tuple[int, float]]:
    """(openTimeSec, ask.close - bid.close) on the BID∩ASK intersection."""
    bid, ask = build_bid_ask_m1(year)
    return [(b.open_time_sec, a.close - b.close) for b, a in zip(bid, ask)]
