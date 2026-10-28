"""Stable candle identity and ordering.

Every candle gets one deterministic unique id derived from the values that
identify it at the source:

    symbol | timeframe | open_time_utc

The id is a pure function of those three values, so the same candle always
produces the same id — across restarts, across machines, and on both sides of
the wire. That property is what makes duplicate suppression possible without
trusting the transport: a receiver can recompute the id from the candle it
received and compare.

Price values are deliberately excluded. Two different candles can share an
OHLC tuple (a flat market repeats closes), so a price-based key would collapse
distinct candles into one and silently drop data.

The id is also the ordering key. Because it embeds ``open_time_utc`` and the
timeframes are strictly nested (M1 divides M5 divides M15 ...), sorting a set
of candles by (open_time_utc, timeframe_rank) yields the exact order the source
produced them. Delivery uses that order so a receiver never sees a candle
before the candle it depends on.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import List, Optional, Sequence

# Canonical nine timeframes in ascending duration order. The index in this list
# is the ``timeframe_rank`` used as the tie-break in ordering.
TIMEFRAMES: List[str] = ["M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"]

# Nominal duration in seconds. Used for gap detection and grid alignment.
# W1 and MN1 are nominal only: real weekly and monthly bars are not a fixed
# number of seconds long (a month is 28..31 days, a week can be 7 or 8 days
# across a DST boundary). They are used for coarse bucketing, never as an
# exact arithmetic step.
TIMEFRAME_SECONDS = {
    "M1": 60,
    "M5": 300,
    "M15": 900,
    "M30": 1800,
    "H1": 3600,
    "H4": 14400,
    "D1": 86400,
    "W1": 604800,
    "MN1": 2592000,
}

# Timeframes whose bar spacing is not a fixed number of seconds. Gap counting
# for these uses consecutive open times rather than arithmetic stepping.
IRREGULAR_TIMEFRAMES = frozenset({"W1", "MN1"})


def timeframe_rank(timeframe: str) -> int:
    """Position of a timeframe in ascending duration order (-1 if unknown)."""
    try:
        return TIMEFRAMES.index(timeframe)
    except ValueError:
        return -1


def is_known_timeframe(timeframe: str) -> bool:
    return timeframe in TIMEFRAME_SECONDS


def _fnv1a_64(raw: str) -> int:
    digest = 0xCBF29CE484222325
    for byte in raw.encode("utf-8"):
        digest ^= byte
        digest = (digest * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return digest


def candle_uid(symbol: str, timeframe: str, open_time_utc: int) -> str:
    """Deterministic unique id for one candle.

    Format: ``<symbol>:<timeframe>:<open_time>@<hash>``

    The human-readable prefix makes a log line or a database row identifiable at
    a glance; the hash suffix makes the id fixed-width and safe to use as a key
    even when a broker symbol contains separators.
    """
    if not symbol:
        raise ValueError("symbol is required for a candle identity")
    if not is_known_timeframe(timeframe):
        raise ValueError(f"unknown timeframe: {timeframe!r}")
    if open_time_utc < 0:
        raise ValueError("open_time_utc must not be negative")
    raw = f"{symbol}|{timeframe}|{int(open_time_utc)}"
    return f"{symbol}:{timeframe}:{int(open_time_utc)}@{_fnv1a_64(raw):016x}"


@dataclass(frozen=True)
class CandleKey:
    """The identity triple, kept structured so it can be indexed directly."""

    symbol: str
    timeframe: str
    open_time_utc: int

    @property
    def uid(self) -> str:
        return candle_uid(self.symbol, self.timeframe, self.open_time_utc)

    @property
    def rank(self) -> int:
        return timeframe_rank(self.timeframe)

    def sort_key(self) -> tuple:
        """Chronological, then by timeframe duration.

        A candle that opens at T is ordered before a candle that opens at T+1;
        among candles opening at the same instant, the shorter timeframe comes
        first so the receiver always has the finer context before the coarser.
        """
        return (self.open_time_utc, self.rank)

    def __str__(self) -> str:
        return self.uid


def sort_keys(keys: Sequence[CandleKey]) -> List[CandleKey]:
    """Chronological, deterministic order for a set of candle keys."""
    return sorted(keys, key=lambda k: k.sort_key())


def expected_sequence(symbol: str, timeframe: str, start_utc: int,
                      count: int) -> List[int]:
    """The ``count`` open times starting at ``start_utc`` on the timeframe grid.

    Only meaningful for regular timeframes. For W1/MN1 the caller must use the
    open times actually reported by the source, because the spacing is not
    constant.
    """
    if count <= 0:
        return []
    if not is_known_timeframe(timeframe):
        raise ValueError(f"unknown timeframe: {timeframe!r}")
    if timeframe in IRREGULAR_TIMEFRAMES:
        raise ValueError(
            f"{timeframe} has irregular spacing; enumerate real open times "
            "from the source instead of stepping arithmetically")
    step = TIMEFRAME_SECONDS[timeframe]
    if start_utc % step != 0:
        raise ValueError(
            f"{start_utc} is not aligned to the {timeframe} grid ({step}s)")
    return [start_utc + i * step for i in range(count)]


def find_gaps(timeframe: str, present_open_times: Sequence[int],
              start_utc: Optional[int] = None,
              end_utc: Optional[int] = None) -> List[tuple]:
    """Contiguous missing ranges inside an observed window.

    Returns a list of ``(gap_start_utc, gap_end_utc, missing_count)`` where the
    bounds are the missing open times themselves. A single missing candle
    yields a one-element range, so a caller can tell "one bar dropped" from "a
    day is missing" without extra bookkeeping.

    Only regular timeframes can be checked arithmetically. For W1/MN1 this
    returns an empty list: a missing weekly bar is indistinguishable from a
    market holiday without a source calendar, and inventing a gap there would
    be a fabricated finding.
    """
    if timeframe in IRREGULAR_TIMEFRAMES:
        return []
    if not is_known_timeframe(timeframe):
        raise ValueError(f"unknown timeframe: {timeframe!r}")
    step = TIMEFRAME_SECONDS[timeframe]
    observed = sorted({int(t) for t in present_open_times})
    if not observed:
        return []
    # Anchor the walk on the first observed open time rather than on the epoch
    # modulus. Bars are only aligned to the epoch in UTC-broker data; a broker
    # whose session grid sits on an offset would otherwise have every bar look
    # off-grid, turning a complete stream into one enormous false gap.
    lo = observed[0] if start_utc is None else int(start_utc)
    hi = observed[-1] if end_utc is None else int(end_utc)
    if hi < lo:
        return []
    present = set(observed)
    gaps: List[tuple] = []
    run_start: Optional[int] = None
    previous: Optional[int] = None
    t = lo
    while t <= hi:
        if t in present:
            if run_start is not None and previous is not None:
                gaps.append((run_start, previous,
                             (previous - run_start) // step + 1))
                run_start = None
        else:
            if run_start is None:
                run_start = t
            previous = t
        t += step
    if run_start is not None and previous is not None:
        gaps.append((run_start, previous, (previous - run_start) // step + 1))
    return gaps


def is_contiguous(timeframe: str, open_times: Sequence[int]) -> bool:
    """True when the supplied open times form an unbroken grid run."""
    if timeframe in IRREGULAR_TIMEFRAMES:
        return True
    if not is_known_timeframe(timeframe):
        raise ValueError(f"unknown timeframe: {timeframe!r}")
    step = TIMEFRAME_SECONDS[timeframe]
    ordered = sorted({int(t) for t in open_times})
    for previous, current in zip(ordered, ordered[1:]):
        if current - previous != step:
            return False
    return True
