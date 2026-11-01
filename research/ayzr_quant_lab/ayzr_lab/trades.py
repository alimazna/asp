"""Canonical trade execution shared by every Phase II experiment.

Phase I duplicated the long/short stop/target construction across scripts, which
is how a bug slips in. This module is the single source of truth:

  * direction is respected when placing stop/target (LONG stop below, SHORT above)
  * levels are flipped for SHORT *before* handing them to the simulator, because
    ``simulate_trade`` works in raw price space
  * an optional one-active-trade gate mirrors the production TradeLifecycleManager

Every experiment imports ``run_engine`` so a fix here fixes all of them.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, List, Optional, Sequence

from .astra_port import RegimeType, SignalDirection, Timeframe
from .backtest import AmbiguousPolicy, Trade, TradeParams, simulate_trade
from .pipeline import Decision


@dataclass
class Geometry:
    stop_mult: float = 2.0
    target_mult: float = 2.0


def _levels(d: Decision, geo: Geometry):
    """Return (stop, target) in price space for the decision's own direction."""
    if d.direction == SignalDirection.LONG:
        return d.entry_price - geo.stop_mult * d.atr, d.entry_price + geo.target_mult * d.atr
    if d.direction == SignalDirection.SHORT:
        return d.entry_price + geo.stop_mult * d.atr, d.entry_price - geo.target_mult * d.atr
    return None, None


def run_engine(bars: Sequence, decisions: Sequence[Decision],
               params: TradeParams, geo: Geometry = Geometry(),
               gate: Optional[Callable[[Decision], bool]] = None,
               one_active: bool = True,
               max_bars: Optional[int] = None) -> List[Trade]:
    """Simulate an engine over one timeframe's decisions.

    ``gate`` decides whether a decision is a valid entry (None = take all).
    ``one_active`` enforces at most one open position, matching production.
    """
    if max_bars is not None:
        params = TradeParams(spread=params.spread, slippage=params.slippage,
                             max_bars=max_bars,
                             ambiguous_policy=params.ambiguous_policy)
    trades: List[Trade] = []
    busy_until = -1
    for d in decisions:
        if d.direction == SignalDirection.NONE or d.atr <= 0:
            continue
        if gate is not None and not gate(d):
            continue
        if one_active and d.bar_index <= busy_until:
            continue
        stop, target = _levels(d, geo)
        if stop is None:
            continue
        t = simulate_trade(d.direction, d.bar_index, bars, stop, target, params)
        if t is None:
            continue
        t.context = {"regime": d.regime.value, "direction": d.direction.name,
                     "rsi": d.rsi, "strength": d.raw_strength,
                     "open_time_sec": d.open_time_sec}
        trades.append(t)
        if one_active:
            busy_until = d.bar_index + t.bars_held
    return trades


def trend_gate(d: Decision) -> bool:
    return d.regime in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN)


def up_gate(d: Decision) -> bool:
    return d.regime == RegimeType.TRENDING_UP


def down_gate(d: Decision) -> bool:
    return d.regime == RegimeType.TRENDING_DOWN
