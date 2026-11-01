"""End-to-end experiment runner.

Drives the whole research loop on the real corpus:

    M1 corpus -> higher timeframes -> decisions -> realistic backtest -> summary

An *engine* here is a callable ``(context) -> Optional[Decision]`` applied per
closed entry bar, where ``context`` bundles the entry-timeframe decision and the
aligned higher-timeframe decisions. This keeps every horizon on the same rig so
SHORT / MEDIUM / LONG and the ASTRA CORE baseline are directly comparable.

Spread is drawn from the real BID/ASK corpus for the requested year, so friction
is the broker's, not a guess.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional, Sequence

from . import astra_port as ap
from .astra_port import Bar, SignalDirection, Timeframe
from .backtest import AmbiguousPolicy, ExitReason, Trade, TradeParams, simulate_trade
from .data import Series, align_grid, build_series, load_m1, spread_series
from .pipeline import Decision, extract_decisions
from .stats import Summary, summarize, summarize_by


@dataclass
class Context:
    """Everything an engine may look at for one decision instant."""
    entry: Decision
    entry_bars: Sequence[Bar]
    # Higher-timeframe decisions, aligned as-of the entry bar's close.
    context_decisions: Dict[str, Decision]


@dataclass
class ExperimentResult:
    name: str
    summary: Summary
    trades: List[Trade]
    decisions: List[Decision]
    median_spread: float


def _aligned_context(series: Dict[Timeframe, Series], tf: Timeframe,
                     as_of_sec: int, window: int) -> Optional[Decision]:
    """The most recent CLOSED decision on `tf` at or before as_of_sec."""
    s = series[tf]
    win = s.window_ending_at(as_of_sec, window)
    if len(win) < 60:
        return None
    decs = extract_decisions(win, tf, window=window)
    return decs[-1] if decs else None


def run_engine(name: str,
               year: int,
               entry_tf: Timeframe,
               engine_fn: Callable[[Context], Optional[Decision]],
               context_tfs: Sequence[Timeframe] = (),
               window: int = 500,
               slippage: float = 0.05,
               max_bars: int = 500,
               ambiguous_policy: AmbiguousPolicy = AmbiguousPolicy.WORST_CASE,
               start_sec: Optional[int] = None,
               end_sec: Optional[int] = None,
               spread_override: Optional[float] = None) -> ExperimentResult:
    bars_m1 = load_m1(year)
    tfs = [entry_tf] + [t for t in context_tfs if t != entry_tf]
    series = build_series(bars_m1, tfs)
    entry_series = series[entry_tf]

    if spread_override is not None:
        spread = spread_override
    else:
        sp = spread_series(year)
        sp_vals = sorted(v for _, v in sp if v > 0)
        spread = sp_vals[len(sp_vals) // 2] if sp_vals else 0.20

    params = TradeParams(spread=spread, slippage=slippage, max_bars=max_bars,
                         ambiguous_policy=ambiguous_policy)

    decisions = extract_decisions(entry_series.bars, entry_tf, window=window)
    trades: List[Trade] = []
    all_decisions: List[Decision] = []

    for d in decisions:
        if start_sec is not None and d.open_time_sec < start_sec:
            continue
        if end_sec is not None and d.open_time_sec >= end_sec:
            continue
        all_decisions.append(d)
        ctx_decisions: Dict[str, Decision] = {}
        for tf in context_tfs:
            cd = _aligned_context(series, tf, d.open_time_sec, window)
            if cd is not None:
                ctx_decisions[tf.value] = cd
        ctx = Context(entry=d, entry_bars=entry_series.bars, context_decisions=ctx_decisions)
        sig = engine_fn(ctx)
        if sig is None or sig.direction == SignalDirection.NONE:
            continue
        trade = simulate_trade(sig.direction, d.bar_index, entry_series.bars,
                               sig.stop, sig.target, params)
        if trade is None:
            continue
        trade.context = {
            "regime": d.regime.value,
            "bias": d.bias.value,
            "strength": sig.raw_strength,
            "direction": sig.direction.value,
            "year": year,
        }
        trades.append(trade)

    return ExperimentResult(name=name, summary=summarize(trades), trades=trades,
                            decisions=all_decisions, median_spread=spread)


def astra_core_engine(ctx: Context) -> Optional[Decision]:
    """The ASTRA CORE baseline: publish the entry-timeframe decision as-is."""
    return ctx.entry if ctx.entry.direction != SignalDirection.NONE else None
