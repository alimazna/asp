#pragma once
// DEC-0022 - Market quality engine.
//
// Combines data quality (from the bus) with spread and volatility conditions
// into a single quality verdict. Market quality is a gate input: a low-quality
// market must not be traded on, even if a signal exists.

#include "data/DataBus.h"
#include "data/TimeframeStateStore.h"
#include "features/FeatureSnapshot.h"
#include "foundation/DataQualityState.h"
#include "mt5/Mt5BridgeContract.h"

#include <string>
#include <vector>

namespace aura {

struct MarketQuality {
    Timeframe timeframe = Timeframe::M15;
    double spreadScore = 0.0;      // 0..1, higher is better
    double liquidityScore = 0.0;   // 0..1
    double volatilityScore = 0.0;  // 0..1 (1 = suitable)
    double overall = 0.0;          // 0..1
    DataQualityState quality = DataQualityState::UNKNOWN;
    bool valid = false;
    std::vector<std::string> notes;
};

struct MarketQualityConfig {
    std::int64_t maxAcceptableSpread = 50;   // in points
    double idealVolatilityLow = 0.001;
    double idealVolatilityHigh = 0.008;
};

class MarketQualityEngine {
public:
    explicit MarketQualityEngine(MarketQualityConfig config = {})
        : config_(config) {}

    MarketQuality evaluate(const std::vector<Bar>& bars,
                           const FeatureSnapshot& features,
                           DataQualityState dataQuality) const;

private:
    MarketQualityConfig config_;
};

}  // namespace aura
