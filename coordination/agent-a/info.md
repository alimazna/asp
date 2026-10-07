# Agent-A - Important Info
> Living document. Updated, not appended.

## Role
Features & Analytics. Owns feature extraction over 3 months x 9 timeframes and the latest 9 closed candles, plus feature tests. Must prove no lookahead and no reward-structure artifact.

## Owned files
- src/analysis/features/
- tests/features/
- coordination/agent-a/

## Forbidden files
- The baseline (base9 / baseold) - READ-ONLY.
- src/foundation/ (frozen contracts), src/models/, bridge/, src/api/, packaging/.
- coordination/<other-agent>/ folders.

## Features / deliverables
- T01 (ACTIVE): analytical feature layer.
  * `src/analysis/features/AnalyticalFeatures.h` — types + ranges.
  * `src/analysis/features/AnalyticalFeatureEngine.{h,cpp}` — engine.
  * `src/analysis/features/FEATURES.md` — formula/range/file:line per feature.
  * `tests/features/AnalyticalFeatureTests.cpp` — 9 unit cases.
  * `tests/features/AnalyticalFeatureLeakageTests.cpp` — 5 no-lookahead cases.
- Per-TF (9 streams): structure (structureTrend, rangePosition, swingAsymmetry),
  candle (body/wick ratios, direction, runBalance), momentum (norm, persistence,
  acceleration), volatility (volatilityRatio, atrRatio), local 9-candle pattern
  (netChangeRatio, higherHighShare, lowerLowShare, patternScore), 3-month context
  (contextTrend, contextVolatility, contextRangePosition).
- Cross-TF: h4M15Agreement, h4D1Agreement, mtfConflictScore,
  h4StructuralAuthority, m15TriggerState.

## Key findings
- Engine takes an explicit `asOfBarOpenSec`; drops future bars before compute,
  so no-lookahead is provable (leakage tests).
- Trigger window constant `kTriggerWindow = 9` (MISSION "latest 9 closed candles").
- `tests/features/` is NOT picked up by CMake (CMakeLists globs tests/*.cpp
  non-recursively); needs a Lead/C build-graph change. Tests verified manually.

## Open questions
- Which broker/account supplies the 3-month × 9-TF dataset (MISSION Q2)?
- Are feature values intended as model inputs for Agent-B, or a standalone
  analytics surface? No consumer contract is defined yet.

## Useful commands
- `g++ -std=c++17 -Wall -Wextra -I src -I tests tests/features/<t>.cpp \
   build/libaura_core.a -o /tmp/<t> -lpthread && /tmp/<t>`
