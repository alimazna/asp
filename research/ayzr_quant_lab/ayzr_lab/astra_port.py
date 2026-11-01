"""Faithful scalar port of ASTRA's decision math.

Every function mirrors a specific C++ source file and constant. This module is
the *reference implementation* for research; it is deliberately written as
plain, slow, readable Python so it can be compared line-by-line against the
C++ and verified numerically against a compiled oracle.

Mirrors:
  src/features/FeatureEngine.{h,cpp}      -> FeatureConfig, compute()
  src/structure/StructureEngine.{h,cpp}   -> StructureConfig, compute()
  src/regime/RegimeEngine.{h,cpp}         -> RegimeConfig, compute()
  src/eligibility/EligibilityEngine.{h,cpp}
  src/signals/SignalEngine.{h,cpp}        -> SignalConfig, generate()

Constants are copied verbatim; any divergence is a bug in this port, not a
licence to change ASTRA. ASTRA CORE is never modified by the research lab.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from enum import Enum
from typing import List, Optional, Sequence


# --------------------------------------------------------------------------
# Enums (mirror src/mt5/Mt5BridgeContract.h, foundation, structure, regime)
# --------------------------------------------------------------------------
class Timeframe(Enum):
    M1 = "M1"
    M5 = "M5"
    M15 = "M15"
    M30 = "M30"
    H1 = "H1"
    H4 = "H4"
    D1 = "D1"
    W1 = "W1"
    MN1 = "MN1"


TIMEFRAME_SECONDS = {
    Timeframe.M1: 60,
    Timeframe.M5: 300,
    Timeframe.M15: 900,
    Timeframe.M30: 1800,
    Timeframe.H1: 3600,
    Timeframe.H4: 14400,
    Timeframe.D1: 86400,
    Timeframe.W1: 604800,
    Timeframe.MN1: 2592000,  # calendar month approximated; never used as a grid
}


class DataQualityState(Enum):
    UNKNOWN = "UNKNOWN"
    INCOMPLETE = "INCOMPLETE"
    INVALID = "INVALID"
    VALID = "VALID"


class StructureBias(Enum):
    BULLISH = "BULLISH"
    BEARISH = "BEARISH"
    RANGE = "RANGE"


class RegimeType(Enum):
    TRENDING_UP = "TRENDING_UP"
    TRENDING_DOWN = "TRENDING_DOWN"
    RANGING = "RANGING"
    QUIET = "QUIET"
    VOLATILE = "VOLATILE"
    UNKNOWN = "UNKNOWN"


class EligibilityDecision(Enum):
    ELIGIBLE = "ELIGIBLE"
    NOT_ELIGIBLE = "NOT_ELIGIBLE"
    UNKNOWN = "UNKNOWN"


class SignalDirection(Enum):
    LONG = "LONG"
    SHORT = "SHORT"
    NONE = "NONE"


# --------------------------------------------------------------------------
# Bar / snapshots
# --------------------------------------------------------------------------
@dataclass
class Bar:
    open_time_sec: int
    open: float
    high: float
    low: float
    close: float
    volume: float = 0.0
    timeframe: Timeframe = Timeframe.M1


@dataclass
class FeatureConfig:
    # src/features/FeatureEngine.h
    fast_period: int = 20
    slow_period: int = 50
    rsi_period: int = 14
    atr_period: int = 14
    momentum_period: int = 10
    volatility_period: int = 20
    min_bars: int = 55


@dataclass
class StructureConfig:
    # src/structure/StructureEngine.h
    swing_lookback: int = 2
    min_swings: int = 2


@dataclass
class RegimeConfig:
    # src/regime/RegimeEngine.h
    trend_slope_threshold: float = 0.0008
    volatile_threshold: float = 0.012
    quiet_threshold: float = 0.0015


@dataclass
class EligibilityConfig:
    # src/eligibility/EligibilityEngine.h
    allow_ranging: bool = True
    allow_volatile: bool = True
    require_valid_structure: bool = True


@dataclass
class SignalConfig:
    # src/signals/SignalEngine.h
    rsi_long_threshold: float = 52.0
    rsi_short_threshold: float = 48.0
    atr_stop_multiplier: float = 1.5
    atr_target_multiplier: float = 2.5


@dataclass
class FeatureSnapshot:
    timeframe: Timeframe = Timeframe.M15
    as_of_bar_open_sec: int = 0
    bar_count: int = 0
    last_close: float = 0.0
    last_open: float = 0.0
    last_high: float = 0.0
    last_low: float = 0.0
    ema_fast: float = 0.0
    ema_slow: float = 0.0
    trend_slope: float = 0.0
    atr: float = 0.0
    volatility: float = 0.0
    rsi: float = 50.0
    momentum: float = 0.0
    body_ratio: float = 0.0
    range_ratio: float = 0.0
    quality: DataQualityState = DataQualityState.UNKNOWN
    valid: bool = False
    detail: str = ""


@dataclass
class StructureSnapshot:
    timeframe: Timeframe = Timeframe.M15
    as_of_bar_open_sec: int = 0
    swings: List[tuple] = field(default_factory=list)
    higher_highs: bool = False
    lower_highs: bool = False
    higher_lows: bool = False
    lower_lows: bool = False
    last_swing_high: float = 0.0
    last_swing_low: float = 0.0
    bias: StructureBias = StructureBias.RANGE
    structure_score: float = 0.0
    quality: DataQualityState = DataQualityState.UNKNOWN
    valid: bool = False
    detail: str = ""


@dataclass
class RegimeState:
    timeframe: Timeframe = Timeframe.M15
    as_of_bar_open_sec: int = 0
    regime: RegimeType = RegimeType.UNKNOWN
    trend_strength: float = 0.0
    volatility_level: float = 0.0
    quality: DataQualityState = DataQualityState.UNKNOWN
    valid: bool = False
    detail: str = ""


@dataclass
class EligibilityState:
    decision: EligibilityDecision = EligibilityDecision.UNKNOWN
    quality: DataQualityState = DataQualityState.UNKNOWN
    valid: bool = False
    reasons: List[str] = field(default_factory=list)


@dataclass
class SignalCandidate:
    timeframe: Timeframe = Timeframe.M15
    as_of_bar_open_sec: int = 0
    direction: SignalDirection = SignalDirection.NONE
    raw_strength: float = 0.0
    reference_price: float = 0.0
    suggested_stop: float = 0.0
    suggested_target: float = 0.0
    rationale: str = ""
    quality: DataQualityState = DataQualityState.UNKNOWN
    valid: bool = False


# --------------------------------------------------------------------------
# FeatureEngine — src/features/FeatureEngine.cpp
# --------------------------------------------------------------------------
def ema(values: Sequence[float], period: int) -> float:
    """C++ FeatureEngine::ema — seed = first value, alpha = 2/(period+1)."""
    if not values or period == 0:
        return 0.0
    alpha = 2.0 / (period + 1.0)
    result = values[0]
    for i in range(1, len(values)):
        result = alpha * values[i] + (1.0 - alpha) * result
    return result


def rsi(closes: Sequence[float], period: int) -> float:
    """C++ FeatureEngine::rsi — simple (not Wilder) over the last `period`."""
    if len(closes) < 2 or period == 0:
        return 50.0
    start = len(closes) - period if len(closes) > period else 1
    gain = 0.0
    loss = 0.0
    for i in range(start, len(closes)):
        change = closes[i] - closes[i - 1]
        if change >= 0.0:
            gain += change
        else:
            loss -= change
    if loss == 0.0:
        return 50.0 if gain == 0.0 else 100.0
    rs = gain / loss
    return 100.0 - (100.0 / (1.0 + rs))


def atr(bars: Sequence[Bar], period: int) -> float:
    """C++ FeatureEngine::atr — simple mean of TR over the last `period`."""
    if len(bars) < 2 or period == 0:
        return 0.0
    start = len(bars) - period if len(bars) > period else 1
    total = 0.0
    count = 0
    for i in range(start, len(bars)):
        prev_close = bars[i - 1].close
        tr = max(
            bars[i].high - bars[i].low,
            abs(bars[i].high - prev_close),
            abs(bars[i].low - prev_close),
        )
        total += tr
        count += 1
    return 0.0 if count == 0 else total / count


def stdev(values: Sequence[float]) -> float:
    """C++ FeatureEngine::stdev — sample stdev (n-1)."""
    if len(values) < 2:
        return 0.0
    mean = sum(values) / len(values)
    variance = sum((v - mean) ** 2 for v in values) / (len(values) - 1)
    return math.sqrt(variance)


def compute_features(bars: Sequence[Bar], timeframe: Timeframe,
                     cfg: FeatureConfig = FeatureConfig()) -> FeatureSnapshot:
    """C++ FeatureEngine::compute — full snapshot from a closed-bar window."""
    snap = FeatureSnapshot(timeframe=timeframe, bar_count=len(bars))
    if len(bars) < cfg.min_bars:
        snap.quality = DataQualityState.INCOMPLETE
        snap.detail = f"insufficient closed bars: {len(bars)} < {cfg.min_bars}"
        return snap

    last = bars[-1]
    snap.as_of_bar_open_sec = last.open_time_sec
    snap.last_close = last.close
    snap.last_open = last.open
    snap.last_high = last.high
    snap.last_low = last.low

    closes = [b.close for b in bars]
    snap.ema_fast = ema(closes, cfg.fast_period)
    snap.ema_slow = ema(closes, cfg.slow_period)
    snap.trend_slope = (
        (snap.ema_fast - snap.ema_slow) / snap.ema_slow if snap.ema_slow != 0.0 else 0.0
    )
    snap.atr = atr(bars, cfg.atr_period)
    snap.rsi = rsi(closes, cfg.rsi_period)

    mom_idx = len(closes) - 1 - cfg.momentum_period if len(closes) > cfg.momentum_period else 0
    snap.momentum = closes[-1] - closes[mom_idx]

    vol_start = len(closes) - cfg.volatility_period - 1 if len(closes) > cfg.volatility_period else 0
    log_returns = []
    for i in range(vol_start + 1, len(closes)):
        if closes[i - 1] > 0.0 and closes[i] > 0.0:
            log_returns.append(math.log(closes[i] / closes[i - 1]))
    snap.volatility = stdev(log_returns)

    rng = last.high - last.low
    snap.body_ratio = abs(last.close - last.open) / rng if rng > 0.0 else 0.0
    snap.range_ratio = rng / snap.atr if snap.atr > 0.0 else 0.0

    if snap.atr <= 0.0 or not math.isfinite(snap.trend_slope):
        snap.quality = DataQualityState.INVALID
        snap.detail = "degenerate feature values (zero ATR or non-finite slope)"
        return snap

    snap.valid = True
    snap.quality = DataQualityState.VALID
    return snap


# --------------------------------------------------------------------------
# StructureEngine — src/structure/StructureEngine.cpp
# --------------------------------------------------------------------------
def compute_structure(bars: Sequence[Bar], features: FeatureSnapshot,
                      cfg: StructureConfig = StructureConfig()) -> StructureSnapshot:
    snap = StructureSnapshot(timeframe=features.timeframe,
                             as_of_bar_open_sec=features.as_of_bar_open_sec)
    lookback = cfg.swing_lookback
    if len(bars) < 2 * lookback + 1:
        snap.quality = DataQualityState.INCOMPLETE
        snap.detail = "insufficient bars for swing detection"
        return snap

    swings: List[tuple] = []
    for i in range(lookback, len(bars) - lookback):
        is_high = True
        is_low = True
        for j in range(1, lookback + 1):
            if bars[i].high <= bars[i - j].high or bars[i].high <= bars[i + j].high:
                is_high = False
            if bars[i].low >= bars[i - j].low or bars[i].low >= bars[i + j].low:
                is_low = False
        if is_high:
            swings.append((bars[i].open_time_sec, bars[i].high, True))
        elif is_low:
            swings.append((bars[i].open_time_sec, bars[i].low, False))

    if len(swings) < cfg.min_swings:
        snap.quality = DataQualityState.INCOMPLETE
        snap.detail = "insufficient swing points"
        return snap
    snap.swings = swings

    last_high = prev_high = last_low = prev_low = 0.0
    highs = lows = 0
    for t, price, is_high in reversed(swings):
        if is_high and highs < 2:
            if highs == 0:
                last_high = price
            else:
                prev_high = price
            highs += 1
        elif (not is_high) and lows < 2:
            if lows == 0:
                last_low = price
            else:
                prev_low = price
            lows += 1
        if highs >= 2 and lows >= 2:
            break

    if highs >= 2:
        snap.higher_highs = last_high > prev_high
        snap.lower_highs = last_high < prev_high
    if lows >= 2:
        snap.higher_lows = last_low > prev_low
        snap.lower_lows = last_low < prev_low
    snap.last_swing_high = last_high
    snap.last_swing_low = last_low

    bull = int(snap.higher_highs) + int(snap.higher_lows)
    bear = int(snap.lower_highs) + int(snap.lower_lows)
    if bull > bear:
        snap.bias = StructureBias.BULLISH
        snap.structure_score = 0.5 + 0.25 * (bull - 1)
    elif bear > bull:
        snap.bias = StructureBias.BEARISH
        snap.structure_score = -0.5 - 0.25 * (bear - 1)
    else:
        snap.bias = StructureBias.RANGE
        snap.structure_score = 0.0
    snap.structure_score = max(-1.0, min(1.0, snap.structure_score))

    snap.valid = True
    snap.quality = DataQualityState.VALID
    return snap


# --------------------------------------------------------------------------
# RegimeEngine — src/regime/RegimeEngine.cpp
# --------------------------------------------------------------------------
def compute_regime(features: FeatureSnapshot, structure: StructureSnapshot,
                   cfg: RegimeConfig = RegimeConfig()) -> RegimeState:
    state = RegimeState(timeframe=features.timeframe,
                        as_of_bar_open_sec=features.as_of_bar_open_sec)
    if not features.valid:
        state.quality = features.quality
        state.detail = "features unavailable: " + features.detail
        return state

    state.volatility_level = features.volatility
    state.trend_strength = abs(features.trend_slope)

    strong_trend = abs(features.trend_slope) >= cfg.trend_slope_threshold
    structure_agrees = (
        (features.trend_slope > 0 and structure.bias == StructureBias.BULLISH)
        or (features.trend_slope < 0 and structure.bias == StructureBias.BEARISH)
    )

    if features.volatility >= cfg.volatile_threshold:
        state.regime = RegimeType.VOLATILE
    elif strong_trend and structure_agrees:
        state.regime = (
            RegimeType.TRENDING_UP if features.trend_slope > 0 else RegimeType.TRENDING_DOWN
        )
    elif features.volatility <= cfg.quiet_threshold and not strong_trend:
        state.regime = RegimeType.QUIET
    else:
        state.regime = RegimeType.RANGING

    state.valid = True
    state.quality = DataQualityState.VALID
    return state


# --------------------------------------------------------------------------
# EligibilityEngine — src/eligibility/EligibilityEngine.cpp
# --------------------------------------------------------------------------
def compute_eligibility(features: FeatureSnapshot, regime: RegimeState,
                        structure: StructureSnapshot,
                        cfg: EligibilityConfig = EligibilityConfig()) -> EligibilityState:
    state = EligibilityState()
    if not features.valid or not regime.valid:
        state.decision = EligibilityDecision.UNKNOWN
        state.quality = features.quality if features.quality != DataQualityState.VALID else regime.quality
        state.reasons.append("features or regime not decision-grade")
        return state

    if cfg.require_valid_structure and not structure.valid:
        state.decision = EligibilityDecision.UNKNOWN
        state.quality = structure.quality
        state.reasons.append("structure not decision-grade")
        return state

    eligible = True
    if regime.regime == RegimeType.VOLATILE and not cfg.allow_volatile:
        eligible = False
        state.reasons.append("volatile regime not permitted by policy")
    elif regime.regime == RegimeType.RANGING and not cfg.allow_ranging:
        eligible = False
        state.reasons.append("ranging regime not permitted by policy")
    elif regime.regime == RegimeType.UNKNOWN:
        eligible = False
        state.reasons.append("unknown regime")

    state.valid = True
    state.quality = DataQualityState.VALID
    state.decision = EligibilityDecision.ELIGIBLE if eligible else EligibilityDecision.NOT_ELIGIBLE
    if eligible:
        state.reasons.append("regime eligible")
    return state


# --------------------------------------------------------------------------
# SignalEngine — src/signals/SignalEngine.cpp
# --------------------------------------------------------------------------
def generate_signal(features: FeatureSnapshot, structure: StructureSnapshot,
                    regime: RegimeState, eligibility: EligibilityState,
                    cfg: SignalConfig = SignalConfig()) -> SignalCandidate:
    cand = SignalCandidate(timeframe=features.timeframe,
                           as_of_bar_open_sec=features.as_of_bar_open_sec,
                           reference_price=features.last_close)

    if not features.valid:
        cand.quality = features.quality
        cand.rationale = "features unavailable"
        return cand
    if eligibility.decision != EligibilityDecision.ELIGIBLE:
        cand.quality = DataQualityState.VALID
        cand.valid = True
        cand.direction = SignalDirection.NONE
        cand.rationale = "not eligible: " + eligibility.decision.value
        return cand

    long_votes = 0
    short_votes = 0
    if structure.bias == StructureBias.BULLISH:
        long_votes += 1
    if structure.bias == StructureBias.BEARISH:
        short_votes += 1
    if regime.regime == RegimeType.TRENDING_UP:
        long_votes += 1
    if regime.regime == RegimeType.TRENDING_DOWN:
        short_votes += 1
    if features.rsi >= cfg.rsi_long_threshold:
        long_votes += 1
    if features.rsi <= cfg.rsi_short_threshold:
        short_votes += 1

    if long_votes > short_votes and long_votes >= 2:
        cand.direction = SignalDirection.LONG
        cand.raw_strength = long_votes / 3.0
    elif short_votes > long_votes and short_votes >= 2:
        cand.direction = SignalDirection.SHORT
        cand.raw_strength = short_votes / 3.0
    else:
        cand.direction = SignalDirection.NONE
        cand.raw_strength = 0.0

    if cand.direction == SignalDirection.LONG:
        cand.suggested_stop = features.last_close - cfg.atr_stop_multiplier * features.atr
        cand.suggested_target = features.last_close + cfg.atr_target_multiplier * features.atr
    elif cand.direction == SignalDirection.SHORT:
        cand.suggested_stop = features.last_close + cfg.atr_stop_multiplier * features.atr
        cand.suggested_target = features.last_close - cfg.atr_target_multiplier * features.atr

    cand.rationale = (
        f"structure={structure.bias.value} regime={regime.regime.value} "
        f"longVotes={long_votes} shortVotes={short_votes}"
    )
    cand.valid = True
    cand.quality = DataQualityState.VALID
    return cand


# --------------------------------------------------------------------------
# Convenience: full ASTRA decision on a single closed-bar window
# --------------------------------------------------------------------------
@dataclass
class AstraDecision:
    features: FeatureSnapshot
    structure: StructureSnapshot
    regime: RegimeState
    eligibility: EligibilityState
    signal: SignalCandidate


def astra_decide(bars: Sequence[Bar], timeframe: Timeframe,
                 feat_cfg: FeatureConfig = FeatureConfig(),
                 struct_cfg: StructureConfig = StructureConfig(),
                 regime_cfg: RegimeConfig = RegimeConfig(),
                 elig_cfg: EligibilityConfig = EligibilityConfig(),
                 signal_cfg: SignalConfig = SignalConfig()) -> AstraDecision:
    """Run ASTRA's full pipeline on one closed-bar window (the CORE reference)."""
    f = compute_features(bars, timeframe, feat_cfg)
    s = compute_structure(bars, f, struct_cfg)
    r = compute_regime(f, s, regime_cfg)
    e = compute_eligibility(f, r, s, elig_cfg)
    sig = generate_signal(f, s, r, e, signal_cfg)
    return AstraDecision(f, s, r, e, sig)
