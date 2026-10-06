// DEC-0023 - Market quality implementation.

#include "market_quality/MarketQualityEngine.h"

#include <algorithm>

namespace aura {

MarketQuality MarketQualityEngine::evaluate(const std::vector<Bar>& bars,
                                            const FeatureSnapshot& features,
                                            DataQualityState dataQuality) const {
    MarketQuality result;
    result.timeframe = features.timeframe;

    if (!isDecisionGrade(dataQuality)) {
        result.quality = dataQuality;
        result.notes.push_back("underlying data not decision-grade");
        return result;
    }

    // Spread: use the most recent bar spread if available.
    if (!bars.empty()) {
        const std::int64_t spread = bars.back().spread;
        result.spreadScore =
            spread <= config_.maxAcceptableSpread
                ? 1.0
                : std::max(0.0, 1.0 - static_cast<double>(spread -
                                                          config_.maxAcceptableSpread) /
                                     static_cast<double>(config_.maxAcceptableSpread));
        result.notes.push_back("spread=" + std::to_string(spread));
    } else {
        result.spreadScore = 0.0;
        result.notes.push_back("no bars for spread assessment");
    }

    // Liquidity proxy: recent tick volume relative to the sample.
    if (bars.size() >= 2) {
        double totalVolume = 0.0;
        for (const auto& bar : bars) totalVolume += static_cast<double>(bar.tickVolume);
        const double average = totalVolume / static_cast<double>(bars.size());
        const double lastVolume = static_cast<double>(bars.back().tickVolume);
        result.liquidityScore =
            average > 0.0 ? std::min(1.0, lastVolume / average) : 0.0;
        result.notes.push_back("avgVolume=" + std::to_string(average));
    }

    // Volatility suitability: best inside the configured band.
    const double vol = features.volatility;
    if (vol < config_.idealVolatilityLow) {
        result.volatilityScore = vol / config_.idealVolatilityLow;
    } else if (vol > config_.idealVolatilityHigh) {
        const double excess = vol - config_.idealVolatilityHigh;
        result.volatilityScore = std::max(0.0, 1.0 - excess * 100.0);
    } else {
        result.volatilityScore = 1.0;
    }
    result.notes.push_back("volatility=" + std::to_string(vol));

    result.overall = (result.spreadScore * 0.4 + result.liquidityScore * 0.3 +
                      result.volatilityScore * 0.3);
    result.valid = true;
    result.quality = DataQualityState::VALID;
    return result;
}

}  // namespace aura
