#pragma once
// RES-0020 - Resilience runtime: candle staleness classification.
//
// Staleness is judged against the last *closed* bar, preserving causal
// correctness. No bar -> UNKNOWN. Future-dated bar -> UNKNOWN (rejected).

#include "foundation/Timestamp.h"
#include "resilience/FreshnessState.h"

#include <cstdint>

namespace aura {

class StaleCandleDetector {
public:
    // lastClosedBarOpen: open time of the most recent closed bar.
    // now: current time.
    // barIntervalMillis: timeframe duration (> 0).
    // allowedIntervals: number of intervals we tolerate before declaring STALE.
    static FreshnessState classify(Timestamp lastClosedBarOpen,
                                   Timestamp now,
                                   std::int64_t barIntervalMillis,
                                   int allowedIntervals = 2);
};

}  // namespace aura
