"""Decision extraction pipeline.

Turns a bar series into a stream of ASTRA decisions — one per closed entry-bar —
fast enough to sweep 1.7M M1 bars, while staying numerically identical to the
scalar reference in ``astra_port``.

The heavy feature arrays (EMA, ATR, RSI, volatility, ratios) are computed with
numpy; structure swings are precomputed once and looked up per decision; the
cheap regime / eligibility / signal stages stay scalar. ``tests/test_pipeline.py``
proves the vectorized features match ``astra_port`` on real windows.

Window discipline: each decision at bar ``i`` sees exactly the last ``W`` closed
bars ending at ``i`` — the same window ASTRA's runtime feeds its engines (the
runtime keeps 500 candles per timeframe). Bar ``i`` is treated as closed; the
fill happens on bar ``i+1`` in the backtester, so no future bar is ever read.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence

import numpy as np

from . import astra_port as ap
from .astra_port import (
    Bar, DataQualityState, EligibilityDecision, EligibilityConfig, FeatureConfig,
    RegimeConfig, RegimeType, SignalConfig, SignalDirection, StructureBias,
    StructureConfig, Timeframe,
)

DEFAULT_WINDOW = 500


# --------------------------------------------------------------------------
# Vectorized feature arrays (validated against astra_port in tests)
# --------------------------------------------------------------------------
def _recursive_ema(x: np.ndarray, period: int) -> np.ndarray:
    """EMA seeded at x[0], alpha = 2/(period+1). Matches ASTRA over long windows."""
    alpha = 2.0 / (period + 1.0)
    out = np.empty_like(x)
    acc = x[0]
    out[0] = acc
    for i in range(1, len(x)):
        acc = alpha * x[i] + (1.0 - alpha) * acc
        out[i] = acc
    return out


def _rolling_simple_rsi(closes: np.ndarray, period: int) -> np.ndarray:
    """ASTRA RSI: simple mean gain/loss over the last `period` changes."""
    n = len(closes)
    out = np.full(n, 50.0)
    diff = np.diff(closes, prepend=closes[0])
    gain = np.where(diff >= 0, diff, 0.0)
    loss = np.where(diff < 0, -diff, 0.0)
    cg = np.concatenate([[0.0], np.cumsum(gain)])
    cl = np.concatenate([[0.0], np.cumsum(loss)])
    for i in range(period, n):
        g = cg[i + 1] - cg[i + 1 - period]
        l = cl[i + 1] - cl[i + 1 - period]
        if l == 0.0:
            out[i] = 50.0 if g == 0.0 else 100.0
        else:
            out[i] = 100.0 - 100.0 / (1.0 + g / l)
    return out


def _rolling_atr(high: np.ndarray, low: np.ndarray, close: np.ndarray,
                 period: int) -> np.ndarray:
    n = len(close)
    out = np.zeros(n)
    tr = np.empty(n)
    tr[0] = high[0] - low[0]
    for i in range(1, n):
        tr[i] = max(high[i] - low[i], abs(high[i] - close[i - 1]), abs(low[i] - close[i - 1]))
    ctr = np.concatenate([[0.0], np.cumsum(tr)])
    for i in range(period, n):
        out[i] = (ctr[i + 1] - ctr[i + 1 - period]) / period
    return out


def _rolling_vol(closes: np.ndarray, period: int) -> np.ndarray:
    n = len(closes)
    out = np.zeros(n)
    logret = np.zeros(n)
    with np.errstate(divide="ignore", invalid="ignore"):
        lr = np.log(closes[1:] / closes[:-1])
    logret[1:] = lr
    for i in range(period + 1, n):
        window = logret[i - period + 1:i + 1]
        out[i] = window.std(ddof=1)
    return out


@dataclass
class FeatureArrays:
    ema_fast: np.ndarray
    ema_slow: np.ndarray
    trend_slope: np.ndarray
    atr: np.ndarray
    rsi: np.ndarray
    volatility: np.ndarray
    momentum: np.ndarray
    body_ratio: np.ndarray
    range_ratio: np.ndarray
    swing_high_idx: np.ndarray
    swing_low_idx: np.ndarray
    swing_high_price: np.ndarray
    swing_low_price: np.ndarray


def compute_arrays(bars: Sequence[Bar], cfg: FeatureConfig = FeatureConfig(),
                   struct_cfg: StructureConfig = StructureConfig()) -> FeatureArrays:
    o = np.array([b.open for b in bars], dtype=float)
    h = np.array([b.high for b in bars], dtype=float)
    l = np.array([b.low for b in bars], dtype=float)
    c = np.array([b.close for b in bars], dtype=float)
    n = len(c)

    ema_f = _recursive_ema(c, cfg.fast_period)
    ema_s = _recursive_ema(c, cfg.slow_period)
    with np.errstate(divide="ignore", invalid="ignore"):
        slope = np.where(ema_s != 0.0, (ema_f - ema_s) / ema_s, 0.0)
    atr = _rolling_atr(h, l, c, cfg.atr_period)
    rsi = _rolling_simple_rsi(c, cfg.rsi_period)
    vol = _rolling_vol(c, cfg.volatility_period)

    mom = np.zeros(n)
    if n > cfg.momentum_period:
        mom[cfg.momentum_period:] = c[cfg.momentum_period:] - c[:-cfg.momentum_period]

    rng = h - l
    body = np.where(rng > 0, np.abs(c - o) / np.where(rng > 0, rng, 1.0), 0.0)
    range_ratio = np.where(atr > 0, rng / np.where(atr > 0, atr, 1.0), 0.0)

    lb = struct_cfg.swing_lookback
    sh_idx, sl_idx = [], []
    for i in range(lb, n - lb):
        is_high = True
        is_low = True
        for j in range(1, lb + 1):
            if h[i] <= h[i - j] or h[i] <= h[i + j]:
                is_high = False
            if l[i] >= l[i - j] or l[i] >= l[i + j]:
                is_low = False
        if is_high:
            sh_idx.append(i)
        elif is_low:
            sl_idx.append(i)

    return FeatureArrays(
        ema_fast=ema_f, ema_slow=ema_s, trend_slope=slope, atr=atr, rsi=rsi,
        volatility=vol, momentum=mom, body_ratio=body, range_ratio=range_ratio,
        swing_high_idx=np.array(sh_idx, dtype=int),
        swing_low_idx=np.array(sl_idx, dtype=int),
        swing_high_price=h[np.array(sh_idx, dtype=int)] if sh_idx else np.array([]),
        swing_low_price=l[np.array(sl_idx, dtype=int)] if sl_idx else np.array([]),
    )


# --------------------------------------------------------------------------
# Decision extraction
# --------------------------------------------------------------------------
@dataclass
class Decision:
    bar_index: int
    open_time_sec: int
    timeframe: Timeframe
    direction: SignalDirection
    raw_strength: float
    entry_price: float
    stop: float
    target: float
    regime: RegimeType
    bias: StructureBias
    rsi: float
    atr: float
    trend_slope: float
    volatility: float
    eligible: bool
    features_valid: bool
    structure_valid: bool


def extract_decisions(bars: Sequence[Bar], timeframe: Timeframe,
                      window: int = DEFAULT_WINDOW,
                      feat_cfg: FeatureConfig = FeatureConfig(),
                      struct_cfg: StructureConfig = StructureConfig(),
                      regime_cfg: RegimeConfig = RegimeConfig(),
                      elig_cfg: EligibilityConfig = EligibilityConfig(),
                      signal_cfg: SignalConfig = SignalConfig()) -> List[Decision]:
    """One ASTRA decision per closed bar, using a rolling `window`-bar history."""
    if len(bars) < feat_cfg.min_bars:
        return []

    arr = compute_arrays(bars, feat_cfg, struct_cfg)
    closes = np.array([b.close for b in bars], dtype=float)
    lb = struct_cfg.swing_lookback
    out: List[Decision] = []

    for i in range(feat_cfg.min_bars - 1, len(bars)):
        start = max(0, i - window + 1)
        if arr.atr[i] <= 0.0 or not np.isfinite(arr.trend_slope[i]):
            continue

        # Structure from precomputed swings, restricted to the window and to
        # swings whose right-side bars are inside the window (index <= i - lb).
        hi_lim = i - lb
        lo_lim = i - lb
        sh = arr.swing_high_idx[(arr.swing_high_idx >= start) & (arr.swing_high_idx <= hi_lim)]
        sl = arr.swing_low_idx[(arr.swing_low_idx >= start) & (arr.swing_low_idx <= lo_lim)]
        if len(sh) + len(sl) < struct_cfg.min_swings:
            continue

        higher_highs = lower_highs = higher_lows = lower_lows = False
        last_sh = prev_sh = last_sl = prev_sl = 0.0
        if len(sh) >= 2:
            last_sh, prev_sh = arr.swing_high_price[arr.swing_high_idx == sh[-1]][0], \
                               arr.swing_high_price[arr.swing_high_idx == sh[-2]][0]
            higher_highs = last_sh > prev_sh
            lower_highs = last_sh < prev_sh
        if len(sl) >= 2:
            last_sl, prev_sl = arr.swing_low_price[arr.swing_low_idx == sl[-1]][0], \
                               arr.swing_low_price[arr.swing_low_idx == sl[-2]][0]
            higher_lows = last_sl > prev_sl
            lower_lows = last_sl < prev_sl

        bull = int(higher_highs) + int(higher_lows)
        bear = int(lower_highs) + int(lower_lows)
        if bull > bear:
            bias = StructureBias.BULLISH
        elif bear > bull:
            bias = StructureBias.BEARISH
        else:
            bias = StructureBias.RANGE

        # Regime (scalar, mirrors RegimeEngine).
        trend_slope = float(arr.trend_slope[i])
        volatility = float(arr.volatility[i])
        strong = abs(trend_slope) >= regime_cfg.trend_slope_threshold
        agrees = ((trend_slope > 0 and bias == StructureBias.BULLISH)
                  or (trend_slope < 0 and bias == StructureBias.BEARISH))
        if volatility >= regime_cfg.volatile_threshold:
            regime = RegimeType.VOLATILE
        elif strong and agrees:
            regime = RegimeType.TRENDING_UP if trend_slope > 0 else RegimeType.TRENDING_DOWN
        elif volatility <= regime_cfg.quiet_threshold and not strong:
            regime = RegimeType.QUIET
        else:
            regime = RegimeType.RANGING

        # Eligibility (scalar).
        eligible = True
        if regime == RegimeType.VOLATILE and not elig_cfg.allow_volatile:
            eligible = False
        elif regime == RegimeType.RANGING and not elig_cfg.allow_ranging:
            eligible = False
        elif regime == RegimeType.UNKNOWN:
            eligible = False

        # Signal (scalar).
        direction = SignalDirection.NONE
        strength = 0.0
        lv = sv = 0
        if bias == StructureBias.BULLISH:
            lv += 1
        if bias == StructureBias.BEARISH:
            sv += 1
        if regime == RegimeType.TRENDING_UP:
            lv += 1
        if regime == RegimeType.TRENDING_DOWN:
            sv += 1
        rsi_v = float(arr.rsi[i])
        if rsi_v >= signal_cfg.rsi_long_threshold:
            lv += 1
        if rsi_v <= signal_cfg.rsi_short_threshold:
            sv += 1
        if eligible:
            if lv > sv and lv >= 2:
                direction = SignalDirection.LONG
                strength = lv / 3.0
            elif sv > lv and sv >= 2:
                direction = SignalDirection.SHORT
                strength = sv / 3.0

        ref = float(closes[i])
        atr_v = float(arr.atr[i])
        if direction == SignalDirection.LONG:
            stop = ref - signal_cfg.atr_stop_multiplier * atr_v
            target = ref + signal_cfg.atr_target_multiplier * atr_v
        elif direction == SignalDirection.SHORT:
            stop = ref + signal_cfg.atr_stop_multiplier * atr_v
            target = ref - signal_cfg.atr_target_multiplier * atr_v
        else:
            stop = target = 0.0

        out.append(Decision(
            bar_index=i, open_time_sec=bars[i].open_time_sec, timeframe=timeframe,
            direction=direction, raw_strength=strength, entry_price=ref,
            stop=stop, target=target, regime=regime, bias=bias, rsi=rsi_v,
            atr=atr_v, trend_slope=trend_slope, volatility=volatility,
            eligible=eligible, features_valid=True, structure_valid=True,
        ))
    return out
