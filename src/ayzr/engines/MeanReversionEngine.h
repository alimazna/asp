#pragma once
// AYZR-ENG-102 — Mean Reversion Engine.
//
// Speciality: detect a statistically stretched deviation from a mean and propose
// a return toward it. It explicitly REFUSES to fade a strong trend: a deviation
// is only actionable when the regime is range-like (measured, not assumed) and
// the deviation exceeds a z-score threshold.

#include "ayzr/engines/StrategyEngine.h"

namespace aura {
namespace ayzr {
namespace eco {

struct MeanReversionConfig {
    std::size_t meanPeriod = 20;
    std::size_t zPeriod = 20;
    double zEntry = 2.0;             // deviation in population sigma
    std::size_t atrPeriod = 14;
    /// If the |slow EMA slope| / ATR exceeds this, the market is trending and we
    /// must not fade it.
    double maxTrendSlopeAtr = 0.15;
    double stopAtrMultiple = 1.5;
    std::vector<double> targetAtrMultiples = {1.0, 1.5};
    std::int64_t expirySeconds = 3600 * 4;
};

class MeanReversionEngine : public StrategyEngine {
public:
    explicit MeanReversionEngine(Timeframe tf = Timeframe::M15,
                                 MeanReversionConfig cfg = {})
        : tf_(tf), cfg_(cfg) {}

    std::string id() const override;
    std::string version() const override { return "1.0.0"; }
    StrategyFamily family() const override { return StrategyFamily::MEAN_REVERSION; }
    Timeframe timeframe() const override { return tf_; }

    std::vector<EngineResult> evaluate(const MarketView& view) const override;

private:
    Timeframe tf_;
    MeanReversionConfig cfg_;
};

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
