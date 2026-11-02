#pragma once
// AYZR-ENG-104 — Market Microstructure Engine.
//
// Speciality: execution conditions and order-flow structure (bid/ask, spread,
// tick imbalance, depth). CRITICAL HONESTY RULE: BiQuote's OHLC feed does NOT
// carry bid/ask, depth or order flow. Until a verified microstructure source is
// wired in, this engine reports UNAVAILABLE and emits a single WARNING result
// that says so. It NEVER derives order flow from OHLC and never invents a
// spread.
//
// The interface accepts a MicrostructureInput so a future verified feed can be
// plugged in without changing the contract; when the input is absent the engine
// stays honest.

#include "ayzr/engines/StrategyEngine.h"

#include <optional>

namespace aura {
namespace ayzr {
namespace eco {

/// Real microstructure input. Every field is optional: a source that supplies
/// only spread still yields a spread observation, while order flow stays absent.
struct MicrostructureInput {
    std::optional<double> bid;
    std::optional<double> ask;
    std::optional<double> last;
    std::optional<std::int64_t> spreadPoints;   // direct spread, if provided
    std::optional<double> tickImbalance;        // (upTicks-downTicks)/total
    std::optional<double> depthImbalance;       // (bidDepth-askDepth)/total
    /// Provenance: which feed supplied this. Empty => no microstructure feed.
    std::string sourceId;
};

struct MicrostructureConfig {
    double wideSpreadPoints = 40.0;    // flag caution above this (XAUUSD points)
    std::int64_t expirySeconds = 3600;
};

class MicrostructureEngine : public StrategyEngine {
public:
    explicit MicrostructureEngine(Timeframe tf = Timeframe::M15,
                                  MicrostructureConfig cfg = {})
        : tf_(tf), cfg_(cfg) {}

    std::string id() const override;
    std::string version() const override { return "1.0.0"; }
    StrategyFamily family() const override { return StrategyFamily::MICROSTRUCTURE; }
    Timeframe timeframe() const override { return tf_; }

    /// Evaluate without microstructure input: always reports unavailability.
    std::vector<EngineResult> evaluate(const MarketView& view) const override;

    /// Evaluate WITH a real microstructure input (used once a feed is verified).
    std::vector<EngineResult> evaluateWith(const MarketView& view,
                                           const MicrostructureInput& micro) const;

    EngineHealth health(const MarketView& view, std::int64_t nowUtc) const override;

private:
    Timeframe tf_;
    MicrostructureConfig cfg_;
};

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
