#pragma once
// DEC-0001 - Feature snapshot: deterministic, derived market features.
//
// Every feature is a pure function of closed bars. A snapshot records the
// timeframe and the source bar count so it can be reproduced exactly.

#include "foundation/DataQualityState.h"
#include "foundation/Timestamp.h"
#include "mt5/Mt5BridgeContract.h"

#include <cstdint>
#include <string>

namespace aura {

struct FeatureSnapshot {
    Timeframe timeframe = Timeframe::M15;
    std::int64_t asOfBarOpenSec = 0;
    std::size_t barCount = 0;

    // Price context.
    double lastClose = 0.0;
    double lastOpen = 0.0;
    double lastHigh = 0.0;
    double lastLow = 0.0;

    // Trend.
    double emaFast = 0.0;
    double emaSlow = 0.0;
    double trendSlope = 0.0;     // normalized fast-slow separation

    // Volatility.
    double atr = 0.0;
    double volatility = 0.0;     // stddev of log returns

    // Momentum.
    double rsi = 50.0;
    double momentum = 0.0;       // close - close[N]

    // Shape.
    double bodyRatio = 0.0;      // |close-open| / range
    double rangeRatio = 0.0;     // range / atr

    DataQualityState quality = DataQualityState::UNKNOWN;
    bool valid = false;
    std::string detail;
};

}  // namespace aura
