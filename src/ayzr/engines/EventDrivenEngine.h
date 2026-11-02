#pragma once
// AYZR-ENG-105 — Event Driven Engine.
//
// Speciality: economic events and news. It may emit a WARNING/context result
// freely, but a TRADE opportunity requires ALL FIVE conditions from the spec:
//   1. a credible, recent news/event,
//   2. fresh market data,
//   3. explicit strategy conditions met,
//   4. explainable entry/SL/TP,
//   5. an independently tested rule set.
//
// The LLM NEVER owns the numeric decision. The engine consumes a structured
// event record (with actual/forecast/previous) and applies deterministic rules;
// any LLM narrative is attached as *explanation only*.

#include "ayzr/engines/StrategyEngine.h"

#include <optional>
#include <string>

namespace aura {
namespace ayzr {
namespace eco {

/// A structured, verified economic event. `verified` gates trade production:
/// an unverified rumour can produce a warning, never a trade.
struct EconomicEvent {
    std::string eventId;
    std::string title;
    std::string sourceId;          // e.g. "reuters", "bls"
    std::int64_t publishedAt = 0;  // epoch seconds
    std::int64_t eventAt = 0;      // when the event is scheduled/happened
    bool verified = false;         // confirmed vs rumour
    std::optional<double> actual;
    std::optional<double> forecast;
    std::optional<double> previous;
    /// Direction the event is expected to push gold: -1..1 (analyst/rule set).
    /// This is a deterministic classification, not an LLM free-text guess.
    std::optional<double> goldBias;
    std::string category;          // "monetary_policy" | "inflation" | ...
    std::string summaryAr;
};

struct EventDrivenConfig {
    std::int64_t maxNewsAgeSeconds = 3600 * 6;   // a stale event is not "recent"
    std::int64_t maxDataAgeSeconds = 3600;       // fresh candles required
    double minAbsBias = 0.25;                    // |goldBias| must clear this
    std::size_t atrPeriod = 14;
    double stopAtrMultiple = 1.5;
    std::vector<double> targetAtrMultiples = {1.5, 2.5};
    std::int64_t expirySeconds = 3600 * 2;
    /// Condition 5: the rule set must be marked as independently tested. When
    /// false the engine can warn but never emit a trade.
    bool ruleSetTested = false;
};

class EventDrivenEngine : public StrategyEngine {
public:
    explicit EventDrivenEngine(Timeframe tf = Timeframe::M15,
                               EventDrivenConfig cfg = {})
        : tf_(tf), cfg_(cfg) {}

    std::string id() const override;
    std::string version() const override { return "1.0.0"; }
    StrategyFamily family() const override { return StrategyFamily::EVENT_DRIVEN; }
    Timeframe timeframe() const override { return tf_; }

    /// With no event, the engine is silent (it has no OHLC-only edge).
    std::vector<EngineResult> evaluate(const MarketView& view) const override;

    /// Evaluate against a specific event. Enforces the five conditions.
    std::vector<EngineResult> evaluateEvent(const MarketView& view,
                                            const EconomicEvent& ev,
                                            std::int64_t nowUtc) const;

    EngineHealth health(const MarketView& view, std::int64_t nowUtc) const override;

private:
    Timeframe tf_;
    EventDrivenConfig cfg_;
};

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
