"""Realistic trade simulator.

Models the lifecycle of a single signal on the entry timeframe with the frictions
a live account actually pays, and refuses to invent information the bars do not
contain:

* Entry is filled on the NEXT bar's open (the bar after the decision bar), never
  on the decision bar's close — that would be lookahead.
* Levels are BID-space (the corpus is bid). A LONG pays the ask on entry
  (bid+spread); a SHORT pays the ask on exit. Either way exactly one full spread
  is charged per round trip.
* Slippage is adverse on entry and on stop/timeout exits; a target is a limit
  fill and does not slip.
* When a single bar spans both the stop and a target, the ORDER is unknown from
  OHLC alone, so the simulator takes the CONSERVATIVE branch (stop first) and
  flags the trade ambiguous. ``ambiguous_policy`` may relax this for sensitivity
  analysis; the default is worst-case.

Nothing here peeks past the decision instant: the window passed to the engine
ends at the decision bar, and fills begin strictly after it.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Optional, Sequence

from .astra_port import Bar, SignalDirection


class ExitReason(Enum):
    TARGET = "TARGET"
    STOP = "STOP"
    TIMEOUT = "TIMEOUT"
    OPEN = "OPEN"


class AmbiguousPolicy(Enum):
    WORST_CASE = "worst_case"   # default: assume stop hit first
    OPTIMISTIC = "optimistic"   # assume target hit first
    SKIP = "skip"               # refuse to score the trade


@dataclass
class TradeParams:
    spread: float = 0.0          # absolute price units, one full spread per round trip
    slippage: float = 0.0        # absolute price units, adverse on entry and stop/timeout
    max_bars: int = 500          # timeout horizon in entry-timeframe bars
    ambiguous_policy: AmbiguousPolicy = AmbiguousPolicy.WORST_CASE


@dataclass
class Trade:
    direction: SignalDirection
    entry_time_sec: int
    entry_price: float
    stop: float
    target: float
    risk: float                       # |entry - stop| in price units
    exit_time_sec: int = 0
    exit_price: float = 0.0
    reason: ExitReason = ExitReason.OPEN
    r_multiple: float = 0.0           # net, after frictions
    gross_r_multiple: float = 0.0     # before frictions
    mae_r: float = 0.0                # max adverse excursion, in R
    mfe_r: float = 0.0                # max favourable excursion, in R
    bars_held: int = 0
    ambiguous: bool = False
    skipped: bool = False
    context: dict = field(default_factory=dict)


def simulate_trade(direction: SignalDirection,
                   decision_bar_index: int,
                   bars: Sequence[Bar],
                   stop: float,
                   target: float,
                   params: TradeParams) -> Optional[Trade]:
    """Simulate one trade entering at the open of bar ``decision_bar_index + 1``.

    ``bars`` must be the entry-timeframe series; ``decision_bar_index`` indexes
    the bar whose close produced the signal. Returns None if there is no next bar
    to fill on.
    """
    entry_idx = decision_bar_index + 1
    if entry_idx >= len(bars):
        return None

    entry_bar = bars[entry_idx]
    is_long = direction == SignalDirection.LONG

    # Fill at the next open. LONG buys at ask (bid + spread); SHORT sells at bid.
    entry_price = entry_bar.open + (params.spread if is_long else 0.0)
    entry_price += params.slippage if is_long else -params.slippage

    risk = abs(entry_price - stop)
    if risk <= 0.0:
        return None

    trade = Trade(direction=direction, entry_time_sec=entry_bar.open_time_sec,
                  entry_price=entry_price, stop=stop, target=target, risk=risk)

    best, worst = entry_price, entry_price
    end_idx = min(len(bars), entry_idx + params.max_bars)

    for k in range(entry_idx, end_idx):
        bar = bars[k]
        best = max(best, bar.high)
        worst = min(worst, bar.low)

        hit_stop = (bar.low <= stop) if is_long else (bar.high >= stop)
        hit_target = (bar.high >= target) if is_long else (bar.low <= target)

        if hit_stop and hit_target:
            trade.ambiguous = True
            if params.ambiguous_policy == AmbiguousPolicy.SKIP:
                trade.skipped = True
                trade.bars_held = k - entry_idx + 1
                trade.exit_time_sec = bar.open_time_sec
                return trade
            if params.ambiguous_policy == AmbiguousPolicy.OPTIMISTIC:
                hit_stop = False
            else:
                hit_target = False

        if hit_stop:
            # Stop exits as a market order: adverse slippage, ask on the buy-back.
            exit_price = stop + (-params.slippage if is_long else params.slippage)
            if not is_long:
                exit_price += params.spread
            trade.exit_price = exit_price
            trade.reason = ExitReason.STOP
            trade.bars_held = k - entry_idx + 1
            trade.exit_time_sec = bar.open_time_sec
            break
        if hit_target:
            # Target is a limit fill: no slippage. SHORT buys back at ask.
            exit_price = target + (params.spread if not is_long else 0.0)
            trade.exit_price = exit_price
            trade.reason = ExitReason.TARGET
            trade.bars_held = k - entry_idx + 1
            trade.exit_time_sec = bar.open_time_sec
            break
    else:
        last = bars[end_idx - 1]
        exit_price = last.close + (-params.slippage if is_long else params.slippage)
        if not is_long:
            exit_price += params.spread
        trade.exit_price = exit_price
        trade.reason = ExitReason.TIMEOUT
        trade.bars_held = end_idx - entry_idx
        trade.exit_time_sec = last.open_time_sec

    if is_long:
        pnl = trade.exit_price - trade.entry_price
        trade.mae_r = max(0.0, (entry_price - worst) / risk)
        trade.mfe_r = max(0.0, (best - entry_price) / risk)
    else:
        pnl = trade.entry_price - trade.exit_price
        trade.mae_r = max(0.0, (best - entry_price) / risk)
        trade.mfe_r = max(0.0, (entry_price - worst) / risk)

    trade.gross_r_multiple = pnl / risk
    trade.r_multiple = pnl / risk
    return trade
