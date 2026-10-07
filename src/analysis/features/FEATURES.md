# Analytical Features — T01 (Agent-A)

Deterministic, causal, interpretable, bounded features for the calibrated
probability system. Implementation:
`src/analysis/features/AnalyticalFeatureEngine.cpp`. Types:
`src/analysis/features/AnalyticalFeatures.h`.

## Causality contract

The engine reads only closed bars. `computeTimeframe` accepts an explicit
`asOfBarOpenSec` decision bar and drops every bar with a later open time before
computing anything (`AnalyticalFeatureEngine.cpp:103`). `asOfBarOpenSec` in the
result is the open time of the last closed bar used. No bar with a later close
time is read, so no future information can enter a feature — this is directly
provable by passing a decision bar in the middle of a series (see
`tests/features/AnalyticalFeatureLeakageTests.cpp`).

## Windows

- **Trigger window** — the latest `kTriggerWindow = 9` closed candles
  (`AnalyticalFeatures.h:31`, used at `AnalyticalFeatureEngine.cpp:125`).
- **Context window** — up to `contextBars = 200` closed candles supplied as the
  "3-month" history for that stream (`AnalyticalFeatureConfig.contextBars`).

## Per-timeframe features

| Feature | Range | Formula | Impl |
|---|---|---|---|
| `bodyRatio` | [0,1] | `|close-open| / (high-low)` of last closed bar | `AnalyticalFeatureEngine.cpp:148` |
| `upperWickRatio` | [0,1] | `(high - max(open,close)) / (high-low)` | `:149` |
| `lowerWickRatio` | [0,1] | `(min(open,close) - low) / (high-low)` | `:151` |
| `candleDirection` | {-1,0,1} | `sign(close-open)` | `:154` |
| `higherHighShare` | [0,1] | `count(high[i]>high[i-1]) / (n-1)` over window | `:164` |
| `lowerLowShare` | [0,1] | `count(low[i]<low[i-1]) / (n-1)` over window | `:165` |
| `structureTrend` | [-1,1] | `(upTransitions - downTransitions) / (2*(n-1))`, up = higher high **or** higher low | `:166`, `:75` |
| `rangePosition` | [0,1] | `(close - windowLow) / (windowHigh - windowLow)` | `:169` |
| `swingAsymmetry` | [-1,1] | `(idx(lastHigh) - idx(lastLow)) / (n-1)`; +ve = high more recent | `:179` |
| `runBalance` | [-1,1] | signed length of trailing same-direction candle run `/ (n-1)` | `:197` |
| `momentumNorm` | [-1,1] | `netMove / (n-1) / meanAbsStep`, netMove = `close[last]-close[first]` | `:206` |
| `momentumPersistence` | [0,1] | `|netMove| / Σ|close[i]-close[i-1]|` | `:207` |
| `momentumAcceleration` | [-1,1] | `(recentSpeed - earlierSpeed) / (recentSpeed + earlierSpeed)` | `:217` |
| `volatilityRatio` | [0,1] | `shortVol / (shortVol + longVol)`, stdev of log returns (window vs context) | `:225`, `:38` |
| `atrRatio` | [0,1] | `windowATR / (windowATR + contextATR)` | `:230`, `:57` |
| `netChangeRatio` | [-1,1] | `netMove / (windowHigh - windowLow)` | `:233` |
| `patternScore` | [-1,1] | `0.5*structureTrend + 0.3*netChangeRatio + 0.2*runBalance` | `:234` |
| `contextTrend` | [-1,1] | `structureTrendOf(contextWindow)` | `:239` |
| `contextVolatility` | [0,1] | `clampUnit(stdev(log returns over context))` | `:240` |
| `contextRangePosition` | [0,1] | `(close - contextLow) / (contextHigh - contextLow)` | `:248` |

## Cross-timeframe features

Computed at `AnalyticalFeatureEngine.cpp:264-322`.

| Feature | Range | Meaning | Impl |
|---|---|---|---|
| `h4M15Agreement` | {-1,0,1} | sign of `structureTrend` where M15 and H4 agree; else 0 | `:287` |
| `h4D1Agreement` | {-1,0,1} | sign where H4 and D1 agree; else 0 | `:290` |
| `mtfConflictScore` | [0,1] | share of available {M15,H4,D1} pairs whose `structureTrend` signs differ | `:306` |
| `h4StructuralAuthority` | [-1,1] | H4 `structureTrend` (the structural-authority state) | `:308` |
| `m15TriggerState` | [-1,1] | `0.5*M15.structureTrend + 0.5*M15.patternScore` (the operational trigger) | `:309` |

M15 is the trigger/operational stream and H4 is the structural authority, per
`docs/AURA_ASTRA_MASTER_UNIFIED_PROJECT_v4.0.md` V4-04 and `DEC-011`.

## Validity and honesty

- A per-timeframe vector is `valid` only with `>= minTriggerBars (3)` bars and a
  non-zero window range; otherwise `quality` is `INCOMPLETE`/`INVALID` and
  `detail` says why (`:117`, `:130`, `:253`).
- A cross vector is `valid` only when both M15 and H4 are valid; a missing D1
  yields `DEGRADED` (not fabricated) (`:312`, `:320`).
- An absent stream in `computeAll` is emitted with `UNKNOWN` quality and
  `valid=false` (`:335`).
- `UNKNOWN` is never treated as fresh or safe (GLOBAL_AI_CODING_RULES #9).

## Determinism

No randomness, no wall-clock reads, no iteration over unordered containers. The
same bar series always produces identical output.
