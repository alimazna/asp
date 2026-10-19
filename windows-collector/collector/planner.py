"""Delivery planning: decide exactly what to send, in what order.

The planner is the answer to three questions, asked in this order:

  1. What does the receiver actually have?  (authoritative, from the receiver)
  2. What do we have?                       (local candle store)
  3. What must be sent, and in what order?   (the plan)

It is deliberately driven by a *set* of unique candle ids on both sides, not by
a single "last candle" watermark. A watermark hides a hole in the middle: if
the receiver holds candles 1..9 but is missing 4 and 5, its watermark says 9
and a watermark-based sender would conclude everything is fine and never
repair the gap. Comparing sets makes the hole visible.

Ordering rule: the plan is a single chronological stream across all
timeframes, ordered by ``(open_time_utc, timeframe_rank)``. Within one instant
the shorter timeframe goes first. The receiver therefore never sees a candle
before the candles it depends on, and never sees a stream restart mid-flight.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence

from .identity import (
    CandleKey,
    find_gaps,
    is_known_timeframe,
    timeframe_rank,
)


@dataclass
class StreamPlan:
    """What to send for one (symbol, timeframe) stream."""

    symbol: str
    timeframe: str
    #: Open times we hold locally and the receiver has not confirmed.
    to_send: List[int] = field(default_factory=list)
    #: Open times we hold and the receiver has confirmed.
    already_have: List[int] = field(default_factory=list)
    #: Open times the receiver has but we do not (informational; we cannot
    #: "unsend" them, and a receiver holding more than us is not an error).
    receiver_only: List[int] = field(default_factory=list)
    #: Contiguous missing ranges inside the window we observed locally.
    #: Each entry is (start_utc, end_utc, missing_count).
    gaps: List[tuple] = field(default_factory=list)

    @property
    def missing_count(self) -> int:
        return len(self.to_send)


@dataclass
class DeliveryPlan:
    """The full plan across every stream."""

    destination: str
    streams: List[StreamPlan] = field(default_factory=list)
    #: Candles to send, in strict chronological order across all timeframes.
    queue: List[CandleKey] = field(default_factory=list)
    #: Local gaps that the source must fill before they can be sent.
    gaps_to_fill: List[tuple] = field(default_factory=list)
    #: Human-readable notes for the status surface (never fabricates a value).
    notes: List[str] = field(default_factory=list)

    @property
    def send_count(self) -> int:
        return len(self.queue)

    @property
    def has_work(self) -> bool:
        return bool(self.queue) or bool(self.gaps_to_fill)


def plan_delivery(destination: str,
                  local_open_times: Dict[tuple, Sequence[int]],
                  remote_open_times: Dict[tuple, Sequence[int]],
                  acknowledged: Optional[Dict[tuple, Sequence[int]]] = None,
                  window_start: Optional[Dict[tuple, int]] = None,
                  window_end: Optional[Dict[tuple, int]] = None) -> DeliveryPlan:
    """Build the plan.

    ``local_open_times``  maps (symbol, timeframe) -> open times we hold.
    ``remote_open_times`` maps (symbol, timeframe) -> open times the receiver
                          reported holding.
    ``acknowledged``      maps (symbol, timeframe) -> open times we already
                          recorded as acknowledged. Used as a fallback when the
                          receiver cannot be reached: a candle we confirmed
                          earlier is not re-planned.
    ``window_start``/``window_end`` optionally bound the local window considered
                          for gap analysis, per stream.
    """
    plan = DeliveryPlan(destination=destination)
    acknowledged = acknowledged or {}
    keys = set(local_open_times) | set(remote_open_times)
    for symbol, timeframe in sorted(
            keys, key=lambda k: (k[0], timeframe_rank(k[1]))):
        local = sorted({int(t) for t in local_open_times.get((symbol, timeframe), [])})
        remote = sorted({int(t) for t in remote_open_times.get((symbol, timeframe), [])})
        acked = sorted({int(t) for t in acknowledged.get((symbol, timeframe), [])})
        stream = StreamPlan(symbol=symbol, timeframe=timeframe)

        # "The receiver has it" is the union of what it reported and what we
        # recorded as acknowledged. A receiver that answers with a full set is
        # authoritative; the local record covers the case where the receiver
        # could not be reached on this run.
        have_remote = set(remote) | set(acked)
        stream.already_have = sorted(set(local) & have_remote)
        stream.receiver_only = sorted(set(remote) - set(local))
        stream.to_send = sorted(set(local) - have_remote)

        if is_known_timeframe(timeframe) and local:
            start = window_start.get((symbol, timeframe)) if window_start else None
            end = window_end.get((symbol, timeframe)) if window_end else None
            stream.gaps = find_gaps(timeframe, local, start, end)

        plan.streams.append(stream)

        for open_time in stream.to_send:
            plan.queue.append(CandleKey(symbol, timeframe, open_time))
        for gap in stream.gaps:
            plan.gaps_to_fill.append((symbol, timeframe) + tuple(gap))

    # One chronological stream. The tie-break on timeframe rank means the
    # receiver always gets the finer context first at a shared instant.
    plan.queue.sort(key=lambda k: k.sort_key())

    for stream in plan.streams:
        if stream.gaps:
            for gap_start, gap_end, missing in stream.gaps:
                plan.notes.append(
                    f"{stream.symbol} {stream.timeframe}: local gap of "
                    f"{missing} candle(s) from {gap_start} to {gap_end}")
        if stream.receiver_only:
            plan.notes.append(
                f"{stream.symbol} {stream.timeframe}: receiver holds "
                f"{len(stream.receiver_only)} candle(s) we do not have locally")
    return plan


def plan_catchup(local_open_times: Sequence[int],
                 remote_open_times: Sequence[int],
                 acknowledged: Sequence[int],
                 max_bars: int) -> List[int]:
    """Bounded, chronological catch-up list for one stream.

    Returns the newest ``max_bars`` unconfirmed local open times, oldest first.
    ``max_bars <= 0`` plans nothing. Acknowledged candles are never re-planned.
    """
    if max_bars <= 0:
        return []
    confirmed = set(int(t) for t in remote_open_times) | set(int(t) for t in acknowledged)
    unconfirmed = sorted({int(t) for t in local_open_times} - confirmed)
    return unconfirmed[-max_bars:]


def plan_prime(local_open_times: Sequence[int], target_bars: int,
               end_utc: Optional[int] = None) -> List[int]:
    """Newest ``target_bars`` local open times for a first-run prime.

    The engine downstream needs a minimum number of closed bars before a
    feature window is decision-grade. Sending only the newest candle leaves it
    blocked until that many live candles have elapsed, so a brand-new
    destination is primed with a bounded window instead.

    ``end_utc`` optionally caps the window at a known decision instant.
    """
    if target_bars <= 0:
        return []
    times = sorted({int(t) for t in local_open_times})
    if end_utc is not None:
        times = [t for t in times if t <= int(end_utc)]
    return times[-target_bars:]


def summarise(plan: DeliveryPlan) -> Dict[str, object]:
    """Compact status view for the UI. Real numbers only; zeros are real."""
    return {
        "destination": plan.destination,
        "send_count": plan.send_count,
        "gap_count": len(plan.gaps_to_fill),
        "streams": [
            {
                "symbol": s.symbol,
                "timeframe": s.timeframe,
                "to_send": len(s.to_send),
                "confirmed": len(s.already_have),
                "gaps": len(s.gaps),
            }
            for s in plan.streams
        ],
        "notes": list(plan.notes),
    }
