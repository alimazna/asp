#pragma once
// RSH-0019 - Deterministic replay engine.
//
// Replays closed bars through the decision chain one bar at a time. At each
// step only bars up to and including the current bar are visible; future bars
// are never accessible. This is the no-lookahead guarantee in executable form.
//
// Replay is read-only with respect to the live backend: it produces decisions
// and outcomes but never issues shadow commands or touches the broker.

#include "confidence/ConfidenceEngine.h"
#include "data/BarNormalizer.h"
#include "eligibility/EligibilityEngine.h"
#include "features/FeatureEngine.h"
#include "regime/RegimeEngine.h"
#include "scoring/ScoreEngine.h"
#include "signals/SignalEngine.h"
#include "structure/StructureEngine.h"

#include <string>
#include <vector>

namespace aura {

struct ReplayBarResult {
    std::int64_t barOpenTimeSec = 0;
    bool decisionProduced = false;
    SignalDirection direction = SignalDirection::NONE;
    double score = 0.0;
    double confidence = 0.0;
    double referencePrice = 0.0;
    std::string decisionId;
    std::string reason;
};

struct ReplayReport {
    bool valid = false;
    std::size_t barsProcessed = 0;
    std::size_t decisionsProduced = 0;
    std::vector<ReplayBarResult> results;
    std::string error;
};

struct ReplayConfig {
    std::size_t minBarsForDecision = 30;
    std::size_t featureLookback = 20;
};

class ReplayEngine {
public:
    explicit ReplayEngine(ReplayConfig config = {}) : config_(config) {}

    // Replay a single timeframe's closed bars in ascending time order. Bars
    // must already be closed; the engine asserts this and refuses otherwise.
    ReplayReport replay(const std::vector<Bar>& bars) const;

    const ReplayConfig& config() const noexcept { return config_; }

private:
    ReplayConfig config_;
};

}  // namespace aura
