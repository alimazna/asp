// RSH-0020 - Replay engine implementation.

#include "replay/ReplayEngine.h"

#include <algorithm>

namespace aura {

ReplayReport ReplayEngine::replay(const std::vector<Bar>& bars) const {
    ReplayReport report;

    if (bars.empty()) {
        report.error = "no bars supplied";
        return report;
    }
    // Causality: bars must be closed (quality observed) and strictly ordered
    // by open time. A candidate/current candle must never be replayed as if it
    // were complete.
    for (std::size_t i = 0; i < bars.size(); ++i) {
        if (bars[i].quality == DataQualityState::UNKNOWN) {
            report.error = "replay requires closed, quality-observed bars (index " +
                           std::to_string(i) + ")";
            return report;
        }
        if (i > 0 && bars[i].openTimeSec <= bars[i - 1].openTimeSec) {
            report.error = "bars not strictly ordered by open time at index " +
                           std::to_string(i);
            return report;
        }
    }

    FeatureEngine featureEngine;
    StructureEngine structureEngine;
    RegimeEngine regimeEngine;
    EligibilityEngine eligibilityEngine;
    SignalEngine signalEngine;
    ScoreEngine scoreEngine;
    ConfidenceEngine confidenceEngine;

    for (std::size_t i = 0; i < bars.size(); ++i) {
        const std::size_t visible = i + 1;   // only current and past bars
        if (visible < config_.minBarsForDecision) continue;

        std::vector<Bar> window(bars.begin(), bars.begin() + visible);
        const Bar& current = bars[i];

        ReplayBarResult result;
        result.barOpenTimeSec = current.openTimeSec;

        const FeatureSnapshot features =
            featureEngine.compute(window, current.timeframe);
        const StructureSnapshot structure =
            structureEngine.compute(window, features);
        const RegimeState regime = regimeEngine.compute(features, structure);
        const EligibilityState eligibility =
            eligibilityEngine.evaluate(features, regime, structure);

        const SignalCandidate candidate =
            signalEngine.generate(features, structure, regime, eligibility);
        const ScoreResult score =
            scoreEngine.compute(features, structure, regime, candidate);
        const ConfidenceResult confidence = confidenceEngine.compute(
            score, structure, regime, candidate, candidate.quality);

        if (candidate.direction != SignalDirection::NONE && score.valid &&
            confidence.valid) {
            result.decisionProduced = true;
            result.direction = candidate.direction;
            result.score = score.score;
            result.confidence = confidence.confidence;
            result.referencePrice = candidate.referencePrice;
            // Deterministic decision identity: timeframe + bar open + direction.
            result.decisionId =
                std::string(toString(current.timeframe)) + "-" +
                std::to_string(current.openTimeSec) + "-" +
                toString(candidate.direction);
            ++report.decisionsProduced;
        } else {
            result.reason = "no decision-grade candidate at this bar";
        }

        report.results.push_back(result);
        ++report.barsProcessed;
    }

    report.valid = true;
    return report;
}

}  // namespace aura
