"""AYZR horizon engines and research hypotheses.

A horizon is a *filter + geometry policy* applied to ASTRA's own candidate — the
lab never rewrites ASTRA's direction logic. Each hypothesis below is a small,
independently testable rule so we can measure its marginal effect on expectancy
rather than shipping a bundle of untested knobs.

Hypotheses (each composable):

  H1 context_align   require the higher-timeframe regime to agree
  H2 min_strength    require a minimum ASTRA rawStrength
  H3 trend_only      exclude RANGING / QUIET / VOLATILE regimes
  H4 rsi_extreme     require RSI beyond a per-horizon distance from 50
  H5 vol_band        require ATR/price within a percentile band
  H6 session         restrict to given UTC hours (London/NY)
  H7 dynamic_rr      override the ATR target/stop multiples
  H8 structure_score require |structure score| >= threshold

The point of the lab is to keep the ones that survive walk-forward + OOS and
discard the rest, including honest negative results.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Sequence

from .astra_port import (
    RegimeType, SignalDirection, StructureBias, Timeframe,
)
from .pipeline import Decision
from .runner import Context


@dataclass
class HorizonSpec:
    name: str
    entry_tf: Timeframe
    context_tfs: List[Timeframe] = field(default_factory=list)
    require_context_align: bool = False
    min_strength: float = 0.0
    trend_only: bool = False
    rsi_extreme: float = 0.0            # |rsi-50| must exceed this
    vol_band: Optional[tuple] = None    # (lo_pct, hi_pct) of ATR/price
    session_hours: Optional[List[int]] = None
    stop_mult: Optional[float] = None   # override
    target_mult: Optional[float] = None
    structure_min: float = 0.0
    allow_ranging: bool = True
    allow_volatile: bool = True


def _hour_utc(open_time_sec: int) -> int:
    return (open_time_sec // 3600) % 24


def _context_agrees(entry_dir: SignalDirection,
                    ctx_decisions: Dict[str, Decision]) -> bool:
    want = RegimeType.TRENDING_UP if entry_dir == SignalDirection.LONG else RegimeType.TRENDING_DOWN
    for d in ctx_decisions.values():
        if d.regime == RegimeType.TRENDING_UP or d.regime == RegimeType.TRENDING_DOWN:
            if d.regime != want:
                return False
    return True


def make_engine(spec: HorizonSpec) -> Callable[[Context], Optional[Decision]]:
    """Build a filter/geometry engine from a HorizonSpec."""
    # ATR percentile band needs a rolling reference; approximate with an absolute
    # ATR/price ratio window supplied directly in vol_band.

    def engine(ctx: Context) -> Optional[Decision]:
        d = ctx.entry
        if d.direction == SignalDirection.NONE:
            return None

        # H3 trend_only
        if spec.trend_only and d.regime not in (RegimeType.TRENDING_UP, RegimeType.TRENDING_DOWN):
            return None
        if not spec.allow_ranging and d.regime == RegimeType.RANGING:
            return None
        if not spec.allow_volatile and d.regime == RegimeType.VOLATILE:
            return None

        # H1 context alignment
        if spec.require_context_align and ctx.context_decisions:
            if not _context_agrees(d.direction, ctx.context_decisions):
                return None

        # H2 min strength
        if d.raw_strength < spec.min_strength:
            return None

        # H4 RSI extreme
        if spec.rsi_extreme > 0.0 and abs(d.rsi - 50.0) < spec.rsi_extreme:
            return None

        # H5 volatility band (ATR / price)
        if spec.vol_band is not None and d.entry_price > 0:
            ratio = d.atr / d.entry_price
            lo, hi = spec.vol_band
            if ratio < lo or ratio > hi:
                return None

        # H6 session
        if spec.session_hours is not None and _hour_utc(d.open_time_sec) not in spec.session_hours:
            return None

        # H7 geometry override
        out = d
        if spec.stop_mult is not None and spec.target_mult is not None and d.atr > 0:
            if d.direction == SignalDirection.LONG:
                out.stop = d.entry_price - spec.stop_mult * d.atr
                out.target = d.entry_price + spec.target_mult * d.atr
            else:
                out.stop = d.entry_price + spec.stop_mult * d.atr
                out.target = d.entry_price - spec.target_mult * d.atr
        return out

    return engine


# --------------------------------------------------------------------------
# Canonical horizon specs (starting points to be validated, not final)
# --------------------------------------------------------------------------
def short_spec() -> HorizonSpec:
    return HorizonSpec(name="SHORT", entry_tf=Timeframe.M5,
                       context_tfs=[Timeframe.M15],
                       require_context_align=False, min_strength=0.60,
                       allow_ranging=True)


def medium_spec() -> HorizonSpec:
    return HorizonSpec(name="MEDIUM", entry_tf=Timeframe.M15,
                       context_tfs=[Timeframe.H1, Timeframe.H4],
                       require_context_align=True, min_strength=0.60,
                       allow_ranging=True)


def long_spec() -> HorizonSpec:
    return HorizonSpec(name="LONG", entry_tf=Timeframe.H4,
                       context_tfs=[Timeframe.D1],
                       require_context_align=True, min_strength=0.60,
                       allow_ranging=False)
