// RES-0021 - Resilience runtime: candle staleness implementation.

#include "resilience/StaleCandleDetector.h"

namespace aura {

FreshnessState StaleCandleDetector::classify(Timestamp lastClosedBarOpen,
                                             Timestamp now,
                                             std::int64_t barIntervalMillis,
                                             int allowedIntervals) {
    if (lastClosedBarOpen.isUnknown() || now.isUnknown()) {
        return FreshnessState::UNKNOWN;
    }
    if (barIntervalMillis <= 0 || allowedIntervals < 0) {
        return FreshnessState::UNKNOWN;
    }
    const std::int64_t age = now.epochMillis() - lastClosedBarOpen.epochMillis();
    if (age < 0) {
        return FreshnessState::UNKNOWN;  // future-dated bar is not trustworthy
    }
    const std::int64_t tolerance =
        barIntervalMillis * static_cast<std::int64_t>(allowedIntervals + 1);
    return age <= tolerance ? FreshnessState::FRESH : FreshnessState::STALE;
}

}  // namespace aura
