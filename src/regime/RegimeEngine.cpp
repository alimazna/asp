// DEC-0009 - Regime engine implementation.

#include "regime/RegimeEngine.h"

#include <cmath>

namespace aura {

RegimeState RegimeEngine::compute(const FeatureSnapshot& features,
                                  const StructureSnapshot& structure) const {
    RegimeState state;
    state.timeframe = features.timeframe;
    state.asOfBarOpenSec = features.asOfBarOpenSec;

    if (!features.valid) {
        state.quality = features.quality;
        state.detail = "features unavailable: " + features.detail;
        return state;
    }

    state.volatilityLevel = features.volatility;
    state.trendStrength = std::fabs(features.trendSlope);

    const bool strongTrend = std::fabs(features.trendSlope) >= config_.trendSlopeThreshold;
    const bool structureAgrees =
        (features.trendSlope > 0 && structure.bias == StructureBias::BULLISH) ||
        (features.trendSlope < 0 && structure.bias == StructureBias::BEARISH);

    if (features.volatility >= config_.volatileThreshold) {
        state.regime = RegimeType::VOLATILE;
    } else if (strongTrend && structureAgrees) {
        state.regime = features.trendSlope > 0 ? RegimeType::TRENDING_UP
                                               : RegimeType::TRENDING_DOWN;
    } else if (features.volatility <= config_.quietThreshold && !strongTrend) {
        state.regime = RegimeType::QUIET;
    } else {
        state.regime = RegimeType::RANGING;
    }

    state.valid = true;
    state.quality = DataQualityState::VALID;
    return state;
}

}  // namespace aura
