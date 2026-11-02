#pragma once
// AYZR-ENG-101 — Trend Following Engine.
//
// Speciality: identify and follow an established trend. It requires MULTIPLE
// independent confirmations (fast/slow EMA alignment, price above/below the
// slow EMA, ADX-like directional strength from a slope measure, and a momentum
// gate) before it emits a direction. A single up/down move is NOT a trend.
//
// It does not trade mean reversion and does not fade strength. When the trend is
// ambiguous it returns NO_TRADE with the reason.

#include "ayzr/engines/StrategyEngine.h"

namespace aura {
namespace ayzr {
namespace eco {

struct TrendConfig {
    std::size_t fastEma = 9;
    std::size_t slowEma = 21;
    std::size_t trendEma = 50;       // longer filter for direction
    std::size_t atrPeriod = 14;
    std::size_t rsiPeriod = 14;
    double minRsiForLong = 52.0;     // momentum must lean the right way
    double maxRsiForShort = 48.0;
    double minSlowEmaSlopeAtr = 0.05; // slow EMA must actually be sloping
    double stopAtrMultiple = 1.5;
    std::vector<double> targetAtrMultiples = {2.0, 3.0};
    std::int64_t expirySeconds = 3600 * 6;
};

class TrendFollowingEngine : public StrategyEngine {
public:
    explicit TrendFollowingEngine(Timeframe tf = Timeframe::M15,
                                  TrendConfig cfg = {})
        : tf_(tf), cfg_(cfg) {}

    std::string id() const override;
    std::string version() const override { return "1.0.0"; }
    StrategyFamily family() const override { return StrategyFamily::TREND_FOLLOWING; }
    Timeframe timeframe() const override { return tf_; }

    std::vector<EngineResult> evaluate(const MarketView& view) const override;

private:
    Timeframe tf_;
    TrendConfig cfg_;
};

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
