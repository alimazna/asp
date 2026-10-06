#pragma once
// DEC-0007 - Regime state: deterministic market regime classification.

#include "foundation/DataQualityState.h"
#include "mt5/Mt5BridgeContract.h"

#include <cstdint>
#include <string>

namespace aura {

enum class RegimeType {
    TRENDING_UP,
    TRENDING_DOWN,
    RANGING,
    VOLATILE,
    QUIET,
    UNKNOWN,
};

inline const char* toString(RegimeType regime) noexcept {
    switch (regime) {
        case RegimeType::TRENDING_UP:   return "TRENDING_UP";
        case RegimeType::TRENDING_DOWN: return "TRENDING_DOWN";
        case RegimeType::RANGING:       return "RANGING";
        case RegimeType::VOLATILE:      return "VOLATILE";
        case RegimeType::QUIET:         return "QUIET";
        case RegimeType::UNKNOWN:       return "UNKNOWN";
    }
    return "UNKNOWN";
}

struct RegimeState {
    Timeframe timeframe = Timeframe::M15;
    std::int64_t asOfBarOpenSec = 0;
    RegimeType regime = RegimeType::UNKNOWN;
    double trendStrength = 0.0;    // 0..1
    double volatilityLevel = 0.0;  // normalized
    DataQualityState quality = DataQualityState::UNKNOWN;
    bool valid = false;
    std::string detail;
};

}  // namespace aura
