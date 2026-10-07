# Analytical Features — T01 (Agent-A)

Deterministic, causal, interpretable, bounded features for the calibrated
probability system. Implementation:
`src/analysis/features/AnalyticalFeatureEngine.cpp`. Types:
`src/analysis/features/AnalyticalFeatures.h`.

## Causality contract

The engine reads only closed bars and computes every feature as of a single
explicit decision instant `asOfBarOpenSec`:

- `computeTimeframe(bars, tf, asOf)` drops every bar with
  `openTimeSec > asOf` before computing anything
  (`AnalyticalFeatureEngine.cpp:131`).
- `computeCross(byTimeframe, asOf)` and `computeAll(byTimeframe, asOf)` thread
  the **same** instant to every stream, so M15/H4/D1 (and all nine timeframes)
  describe one moment (`:285`, `:357`).
- When `asOf < 0`, the engine defaults to the latest closed-bar open time across
  the supplied streams (`latestOpenAcross`, `:99`) — the max of observed bars,
  never a wall-clock "now". This default is causal: no stream reads a bar after
  it.
- `asOfBarOpenSec` on every result (`TimeframeFeatures`, `CrossTimeframeFeatures`,
  `AnalyticalFeatureSet`) is that decision instant.

No bar with a later open time is read, so no future information can enter a
feature. This is provable: appending or mutating M15/H4/D1 future bars does not
change a feature set recomputed at the old instant, and every per-timeframe
vector reports the set's common instant — see
`tests/features/AnalyticalFeatureLeakageTests.cpp`.

## Windows

- **Trigger window** — the latest `kTriggerWindow = 9` closed candles
  (`AnalyticalFeatures.h:31`, used at `AnalyticalFeatureEngine.cpp:147`).
- **Context window** — up to `contextBars = 200` closed candles supplied as the
  "3-month" history for that stream (`AnalyticalFeatureConfig.contextBars`).

## Per-timeframe features

| Feature | Range | Formula | Impl |
|---|---|---|---|
| `bodyRatio` | [0,1] | `|close-open| / (high-low)` of last closed bar | `:170` |
| `upperWickRatio` | [0,1] | `(high - max(open,close)) / (high-low)` | `:171` |
| `lowerWickRatio` | [0,1] | `(min(open,close) - low) / (high-low)` | `:173` |
| `candleDirection` | {-1,0,1} | `sign(close-open)` | `:176` |
| `higherHighShare` | [0,1] | `count(high[i]>high[i-1]) / (n-1)` over window | `:186` |
| `lowerLowShare` | [0,1] | `count(low[i]<low[i-1]) / (n-1)` over window | `:187` |
| `structureTrend` | [-1,1] | `(upTransitions - downTransitions) / (2*(n-1))`, up = higher high **or** higher low | `:188`, `:75` |
| `rangePosition` | [0,1] | `(close - windowLow) / (windowHigh - windowLow)` | `:191` |
| `swingAsymmetry` | [-1,1] | `(idx(lastHigh) - idx(lastLow)) / (n-1)`; +ve = high more recent | `:201` |
| `runBalance` | [-1,1] | signed length of trailing same-direction candle run `/ (n-1)` | `:219` |
| `momentumNorm` | [-1,1] | `netMove / (n-1) / meanAbsStep`, netMove = `close[last]-close[first]` | `:228` |
| `momentumPersistence` | [0,1] | `|netMove| / Σ|close[i]-close[i-1]|` | `:229` |
| `momentumAcceleration` | [-1,1] | `(recentSpeed - earlierSpeed) / (recentSpeed + earlierSpeed)` | `:239` |
| `volatilityRatio` | [0,1] | `shortVol / (shortVol + longVol)`, stdev of log returns (window vs context) | `:247`, `:38` |
| `atrRatio` | [0,1] | `windowATR / (windowATR + contextATR)` | `:252`, `:57` |
| `netChangeRatio` | [-1,1] | `netMove / (windowHigh - windowLow)` | `:255` |
| `patternScore` | [-1,1] | `0.5*structureTrend + 0.3*netChangeRatio + 0.2*runBalance` | `:256` |
| `contextTrend` | [-1,1] | `structureTrendOf(contextWindow)` | `:261` |
| `contextVolatility` | [0,1] | `clampUnit(stdev(log returns over context))` | `:262` |
| `contextRangePosition` | [0,1] | `(close - contextLow) / (contextHigh - contextLow)` | `:270` |

## Cross-timeframe features

Computed at `AnalyticalFeatureEngine.cpp:285-355`.

| Feature | Range | Meaning | Impl |
|---|---|---|---|
| `h4M15Agreement` | {-1,0,1} | sign of `structureTrend` where M15 and H4 agree; else 0 | `:319` |
| `h4D1Agreement` | {-1,0,1} | sign where H4 and D1 agree; else 0 | `:322` |
| `mtfConflictScore` | [0,1] | share of available {M15,H4,D1} pairs whose `structureTrend` signs differ | `:338` |
| `h4StructuralAuthority` | [-1,1] | H4 `structureTrend` (the structural-authority state) | `:340` |
| `m15TriggerState` | [-1,1] | `0.5*M15.structureTrend + 0.5*M15.patternScore` (the operational trigger) | `:341` |

M15 is the trigger/operational stream and H4 is the structural authority, per
`docs/AURA_ASTRA_MASTER_UNIFIED_PROJECT_v4.0.md` V4-04 and `DEC-011`.

## Validity and honesty

- A per-timeframe vector is `valid` only with `>= minTriggerBars (3)` bars and a
  non-zero window range; otherwise `quality` is `INCOMPLETE`/`INVALID` and
  `detail` says why (`:140`, `:152`, `:275`).
- A cross vector is `valid` only when both M15 and H4 are valid; a missing D1
  yields `DEGRADED` (not fabricated) (`:344`, `:352`).
- An absent stream in `computeAll` is emitted with `UNKNOWN` quality and
  `valid=false` (`:374`).
- `UNKNOWN` is never treated as fresh or safe (GLOBAL_AI_CODING_RULES #9).

## RULE A — direction neutrality

A feature layer has no target/stop, but it must not smuggle in a long/short
bias. The engine is direction-neutral under the mirror
`(o,h,l,c) -> (K-o, K-l, K-h, K-c)`, proven in
`tests/features/AnalyticalFeatureRuleATests.cpp`:

- sign features are **antisymmetric** (`x' == -x`): `structureTrend`,
  `runBalance`, `momentumNorm`, `netChangeRatio`, `patternScore`,
  `swingAsymmetry`, `contextTrend`, `candleDirection`, and the cross features
  `h4StructuralAuthority`, `m15TriggerState`, `h4M15Agreement`, `h4D1Agreement`;
- position features **reflect about 0.5** (`x' == 1 - x`): `rangePosition`,
  `contextRangePosition`;
- extreme features **swap**: `upperWick' == lowerWick`, `higherHigh' == lowerLow`;
- magnitude features are **invariant**: `bodyRatio`, `momentumPersistence`,
  `momentumAcceleration`, `atrRatio`, `volatilityRatio` (the log-return share is
  invariant to second order — reflection maps `r` to `log(1-r)`, not
  `-log(1+r)`, a bounded magnitude asymmetry, not a directional preference);
- a flat market yields exactly zero for every sign feature.

## Pinned vs unpinned (caller contract)

- **Pinned** (`asOf >= 0`): a reproducible snapshot. Recomputing the same bars
  at the same instant is invariant to bars appended afterwards. Use this for
  training rows, backtests, and any stored evidence.
- **Unpinned** (`asOf < 0`): the instant advances to the latest observed bar as
  the feed grows. Convenient for a live loop, but not reproducible across time —
  pin the instant when recording a result.

## Determinism

No randomness, no wall-clock reads, no iteration over unordered containers. The
same bar series always produces identical output.
