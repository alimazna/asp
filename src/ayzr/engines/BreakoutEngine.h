#pragma once
// AYZR-ENG-103 — Breakout Engine.
//
// Speciality: detect a confirmed break of a well-defined range. The canonical
// setup is the Asian-session range watched for a London-session break, but the
// engine generalises: any consolidation box of `lookback` bars forms the range,
// and a break requires (a) close beyond the box by at least `bufferAtr`*ATR and
// (b) a volatility-compression precondition so we are not chasing noise.
//
// The session open ACTIVATES monitoring; it never auto-fires a trade. The engine
// emits a DETECTED/PENDING_CONFIRMATION event, never a PUBLISHED one.

#include "ayzr/engines/StrategyEngine.h"

namespace aura {
namespace ayzr {
namespace eco {

struct BreakoutConfig {
    std::size_t rangeLookback = 20;   // bars forming the box
    std::size_t atrPeriod = 14;
    double bufferAtr = 0.25;          // break must clear the edge by this * ATR
    double maxCompressionAtr = 1.6;   // range height / ATR must be <= this
    double stopAtrMultiple = 1.2;
    std::vector<double> targetAtrMultiples = {1.5, 2.5};
    std::int64_t expirySeconds = 3600 * 3;
};

class BreakoutEngine : public StrategyEngine {
public:
    explicit BreakoutEngine(Timeframe tf = Timeframe::M15,
                            BreakoutConfig cfg = {})
        : tf_(tf), cfg_(cfg) {}

    std::string id() const override;
    std::string version() const override { return "1.0.0"; }
    StrategyFamily family() const override { return StrategyFamily::BREAKOUT; }
    Timeframe timeframe() const override { return tf_; }

    std::vector<EngineResult> evaluate(const MarketView& view) const override;

private:
    Timeframe tf_;
    BreakoutConfig cfg_;
};

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
