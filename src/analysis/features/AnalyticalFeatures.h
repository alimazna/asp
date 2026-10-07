#pragma once
// Agent-A (T01) - Analytical feature vector: deterministic, causal, bounded.
//
// Every field below is a PURE function of the closed bars passed to the engine.
// The engine reads only bars whose close time is at or before the decision bar
// (`asOfBarOpenSec`), so no future information can enter a feature.
//
// Conventions:
//   * Bounded signed features live in [-1, 1]  (see clampSigned).
//   * Bounded share/ratio features live in [0, 1] (see clampUnit).
//   * Discrete directional features are -1, 0 or +1.
//   * A feature vector is only `valid` when its inputs were sufficient and
//     non-degenerate. Invalid vectors carry a `quality` and a human `detail`.
//
// The mission "trigger window" is the latest 9 closed candles per timeframe.
// The mission "context window" is the supplied 3-month history for that stream.

#include "data/BarNormalizer.h"
#include "foundation/DataQualityState.h"
#include "mt5/Mt5BridgeContract.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aura {

// Mission trigger window: the latest 9 closed candles per timeframe.
inline constexpr std::size_t kTriggerWindow = 9;

// Per-timeframe feature vector. See src/analysis/features/FEATURES.md for the
// exact formula and range of every field.
struct TimeframeFeatures {
    Timeframe timeframe = Timeframe::M15;
    std::int64_t asOfBarOpenSec = 0;   // decision instant this vector describes
    std::size_t barsAvailable = 0;     // context (3-month) bars supplied
    std::size_t windowUsed = 0;        // trigger bars actually used (<= 9)

    // --- Price structure ---------------------------------------------------
    double structureTrend = 0.0;       // [-1,1] HH/HL vs LH/LL balance in the window
    double rangePosition = 0.0;        // [0,1]  last close inside window low..high
    double swingAsymmetry = 0.0;       // [-1,1] recency of last high vs last low

    // --- Candle behaviour (last closed candle) -----------------------------
    double bodyRatio = 0.0;            // [0,1]  |close-open| / range
    double upperWickRatio = 0.0;       // [0,1]  upper wick / range
    double lowerWickRatio = 0.0;       // [0,1]  lower wick / range
    double candleDirection = 0.0;      // {-1,0,1} sign of close-open
    double runBalance = 0.0;           // [-1,1] signed consecutive same-direction run

    // --- Momentum ----------------------------------------------------------
    double momentumNorm = 0.0;         // [-1,1] net move normalised by typical move
    double momentumPersistence = 0.0;  // [0,1]  share of moves aligned with net move
    double momentumAcceleration = 0.0; // [-1,1] recent vs earlier per-bar speed

    // --- Volatility / regime ----------------------------------------------
    double volatilityRatio = 0.0;      // [0,1]  short vol / (short vol + long vol)
    double atrRatio = 0.0;             // [0,1]  window ATR / (window ATR + context ATR)

    // --- Local 9-candle pattern -------------------------------------------
    double netChangeRatio = 0.0;       // [-1,1] (close_last - close_first) / window range
    double higherHighShare = 0.0;      // [0,1]  share of higher-high transitions
    double lowerLowShare = 0.0;        // [0,1]  share of lower-low transitions
    double patternScore = 0.0;         // [-1,1] blended local pattern strength

    // --- 3-month context window -------------------------------------------
    double contextTrend = 0.0;         // [-1,1] structure trend over the context window
    double contextVolatility = 0.0;    // [0,1]  squashed log-return stdev
    double contextRangePosition = 0.0; // [0,1]  last close inside context low..high

    DataQualityState quality = DataQualityState::UNKNOWN;
    bool valid = false;
    std::string detail;
};

// Cross-timeframe feature vector. Requires M15 (trigger) and H4 (structural
// authority); D1 is used for long-horizon agreement and may be absent.
struct CrossTimeframeFeatures {
    std::int64_t asOfBarOpenSec = 0;

    double h4M15Agreement = 0.0;         // {-1,0,1} sign agreement of structure trend
    double h4D1Agreement = 0.0;          // {-1,0,1} sign agreement of structure trend
    double mtfConflictScore = 0.0;       // [0,1]  share of disagreeing TF pairs
    double h4StructuralAuthority = 0.0;  // [-1,1] H4 structural bias (authority state)
    double m15TriggerState = 0.0;        // [-1,1] M15 operational trigger state

    bool m15Available = false;
    bool h4Available = false;
    bool d1Available = false;

    DataQualityState quality = DataQualityState::UNKNOWN;
    bool valid = false;
    std::string detail;
};

// The full analytical feature set for one decision instant.
struct AnalyticalFeatureSet {
    std::int64_t asOfBarOpenSec = 0;              // common decision instant
    std::vector<TimeframeFeatures> perTimeframe;  // canonical M1..MN1 order
    CrossTimeframeFeatures cross;
    DataQualityState quality = DataQualityState::UNKNOWN;
    bool valid = false;
    std::string detail;
};

}  // namespace aura
