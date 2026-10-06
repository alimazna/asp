// DEC-0017 - Confidence engine implementation.

#include "confidence/ConfidenceEngine.h"

#include <algorithm>

namespace aura {

ConfidenceResult ConfidenceEngine::compute(const ScoreResult& score,
                                           const StructureSnapshot& structure,
                                           const RegimeState& regime,
                                           const SignalCandidate& candidate,
                                           DataQualityState inputQuality) const {
    ConfidenceResult result;

    // Confidence is only defined over decision-grade input.
    if (!isDecisionGrade(inputQuality)) {
        result.quality = inputQuality;
        result.factors.push_back("input not decision-grade");
        return result;
    }
    if (!score.valid || !candidate.valid) {
        result.quality = DataQualityState::UNKNOWN;
        result.factors.push_back("score or signal unavailable");
        return result;
    }

    if (candidate.direction == SignalDirection::NONE) {
        result.valid = true;
        result.quality = DataQualityState::VALID;
        result.confidence = 0.0;
        result.factors.push_back("no directional candidate");
        return result;
    }

    // Base confidence from the score, capped below certainty: a heuristic
    // score can never justify absolute confidence.
    double confidence = (score.score / 100.0) * 0.8;
    result.factors.push_back("score_base=" + std::to_string(confidence));

    // Agreement bonus: structure and regime both aligned with direction.
    const bool structureAligned =
        (candidate.direction == SignalDirection::LONG &&
         structure.bias == StructureBias::BULLISH) ||
        (candidate.direction == SignalDirection::SHORT &&
         structure.bias == StructureBias::BEARISH);
    const bool regimeAligned =
        (candidate.direction == SignalDirection::LONG &&
         regime.regime == RegimeType::TRENDING_UP) ||
        (candidate.direction == SignalDirection::SHORT &&
         regime.regime == RegimeType::TRENDING_DOWN);
    if (structureAligned) confidence += 0.1;
    if (regimeAligned) confidence += 0.1;
    result.factors.push_back("structure_aligned=" +
                             std::string(structureAligned ? "true" : "false"));
    result.factors.push_back("regime_aligned=" +
                             std::string(regimeAligned ? "true" : "false"));

    confidence = std::max(0.0, std::min(0.95, confidence));
    result.confidence = confidence;
    result.valid = true;
    result.quality = DataQualityState::VALID;
    return result;
}

}  // namespace aura
