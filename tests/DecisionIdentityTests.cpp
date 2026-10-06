// TST-0008 - Deterministic decision IDs and duplicate prevention.

#include "TestHarness.h"

#include "confidence/ConfidenceEngine.h"
#include "eligibility/EligibilityEngine.h"
#include "features/FeatureEngine.h"
#include "ledger/PredictionLedger.h"
#include "regime/RegimeEngine.h"
#include "scoring/ScoreEngine.h"
#include "signals/SignalEngine.h"
#include "structure/StructureEngine.h"

#include <cmath>
#include <string>
#include <vector>

using namespace aura;

namespace {

constexpr std::int64_t kM15 = 15 * 60;

// A deterministic trending series: reproducible, no randomness.
std::vector<Bar> trendingBars(std::size_t count, double start, double slope) {
    std::vector<Bar> bars;
    for (std::size_t i = 0; i < count; ++i) {
        const double base = start + slope * static_cast<double>(i);
        const double wobble = std::sin(static_cast<double>(i) * 0.7) * 0.8;
        const double open = base + wobble;
        const double close = base + slope * 0.6;
        const double high = std::max(open, close) + 0.6;
        const double low = std::min(open, close) - 0.6;
        bars.push_back(test::makeBar(Timeframe::M15,
                                     static_cast<std::int64_t>(i) * kM15, open,
                                     high, low, close));
    }
    return bars;
}

struct ChainResult {
    SignalDirection direction = SignalDirection::NONE;
    double score = 0.0;
    double confidence = 0.0;
    std::string decisionId;
};

// Runs the deterministic decision chain over a bar window and derives the
// decision identity from (timeframe, closed-bar open time, direction).
ChainResult runChain(const std::vector<Bar>& bars) {
    ChainResult result;
    const FeatureEngine featureEngine;
    const StructureEngine structureEngine;
    const RegimeEngine regimeEngine;
    const EligibilityEngine eligibilityEngine;
    const SignalEngine signalEngine;
    const ScoreEngine scoreEngine;
    const ConfidenceEngine confidenceEngine;

    const FeatureSnapshot features = featureEngine.compute(bars, Timeframe::M15);
    const StructureSnapshot structure = structureEngine.compute(bars, features);
    const RegimeState regime = regimeEngine.compute(features, structure);
    const EligibilityState eligibility =
        eligibilityEngine.evaluate(features, regime, structure);
    const SignalCandidate candidate =
        signalEngine.generate(features, structure, regime, eligibility);
    const ScoreResult score =
        scoreEngine.compute(features, structure, regime, candidate);
    const ConfidenceResult confidence = confidenceEngine.compute(
        score, structure, regime, candidate, candidate.quality);

    result.direction = candidate.direction;
    result.score = score.score;
    result.confidence = confidence.confidence;
    result.decisionId = std::string(toString(Timeframe::M15)) + "-" +
                        std::to_string(bars.back().openTimeSec) + "-" +
                        toString(candidate.direction);
    return result;
}

}  // namespace

TEST_CASE(decision_chain_is_deterministic) {
    const std::vector<Bar> bars = trendingBars(90, 2000.0, 0.35);
    const ChainResult first = runChain(bars);
    const ChainResult second = runChain(bars);

    CHECK_EQ(first.decisionId, second.decisionId);
    CHECK_EQ(static_cast<int>(first.direction), static_cast<int>(second.direction));
    CHECK_EQ(first.score, second.score);
    CHECK_EQ(first.confidence, second.confidence);
    CHECK(!first.decisionId.empty());
}

TEST_CASE(decision_identity_depends_on_closed_bar) {
    const std::vector<Bar> bars = trendingBars(90, 2000.0, 0.35);
    std::vector<Bar> shifted = bars;
    for (auto& bar : shifted) bar.openTimeSec += kM15;   // next bar window

    const ChainResult a = runChain(bars);
    const ChainResult b = runChain(shifted);
    // A different closed bar must not collide with the same identity.
    CHECK(a.decisionId != b.decisionId);
}

TEST_CASE(ledger_rejects_duplicate_decision_identity) {
    const std::vector<Bar> bars = trendingBars(90, 2000.0, 0.35);
    const ChainResult result = runChain(bars);

    PredictionLedger ledger;
    PredictionRecord record;
    record.decisionId = EntityId(result.decisionId);
    record.timeframe = Timeframe::M15;
    record.asOfBarOpenSec = bars.back().openTimeSec;
    record.direction = result.direction;
    record.score = result.score;
    record.confidence = result.confidence;
    record.probabilityCalibrated = false;
    record.recordedAt = Timestamp::fromEpochMillis(bars.back().openTimeSec * 1000);

    CHECK(ledger.append(record));
    // Re-appending the same deterministic identity is rejected, never
    // overwritten.
    CHECK(!ledger.append(record));
    CHECK_EQ(ledger.size(), static_cast<std::size_t>(1));
    CHECK(ledger.contains(EntityId(result.decisionId)));
}

TEST_CASE(score_and_confidence_are_separate_not_probability) {
    const std::vector<Bar> bars = trendingBars(90, 2000.0, 0.35);
    const ChainResult result = runChain(bars);
    // Score is a 0..100 ranking value; confidence is a 0..1 meta-measure.
    CHECK(result.score >= 0.0 && result.score <= 100.0);
    CHECK(result.confidence >= 0.0 && result.confidence <= 1.0);
}
