#pragma once
// RES-0006 - Resilience contract: market-data freshness.
//
// FRESH is the only state that may be treated as current. STALE is explicitly
// not fresh, and UNKNOWN is never assumed fresh.

#include "foundation/DataQualityState.h"
#include "foundation/Timestamp.h"

#include <cstdint>
#include <string>

namespace aura {

enum class FreshnessState {
    FRESH,
    STALE,
    UNKNOWN,
};

inline const char* toString(FreshnessState f) noexcept {
    switch (f) {
        case FreshnessState::FRESH:   return "FRESH";
        case FreshnessState::STALE:   return "STALE";
        case FreshnessState::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

struct FreshnessInfo {
    FreshnessState state = FreshnessState::UNKNOWN;
    Timestamp lastUpdate;
    std::int64_t ageMillis = -1;
    std::int64_t maxAgeMillis = -1;

    bool isFresh() const noexcept { return state == FreshnessState::FRESH; }

    // Data quality implied by freshness: stale data is never decision-grade.
    DataQualityState impliedQuality() const noexcept {
        if (state == FreshnessState::FRESH) return DataQualityState::VALID;
        if (state == FreshnessState::STALE) return DataQualityState::STALE;
        return DataQualityState::UNKNOWN;
    }
};

}  // namespace aura
