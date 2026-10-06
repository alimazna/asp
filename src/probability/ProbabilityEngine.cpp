// DEC-0019 - Probability boundary implementation.

#include "probability/ProbabilityEngine.h"

#include <algorithm>

namespace aura {

ProbabilityResult ProbabilityEngine::compute(const ScoreResult& score,
                                             const ConfidenceResult& confidence,
                                             const SignalCandidate& candidate) const {
    ProbabilityResult result;

    if (!score.valid || !confidence.valid) {
        result.quality = DataQualityState::UNKNOWN;
        result.notes.push_back("score or confidence unavailable");
        return result;
    }
    if (candidate.direction == SignalDirection::NONE) {
        result.valid = true;
        result.quality = DataQualityState::VALID;
        result.band = {0.0, 0.0, 0.0};
        result.notes.push_back("no directional candidate");
        return result;
    }

    // Map confidence into a neutral band centred at 0.5. This is a heuristic
    // mapping, not a statistical estimate.
    const double halfWidth = std::max(0.02, (1.0 - confidence.confidence) * 0.25);
    const double estimate = 0.5 + (confidence.confidence - 0.5) * 0.3;
    double lower = std::max(0.0, estimate - halfWidth);
    double upper = std::min(1.0, estimate + halfWidth);

    result.band = {estimate, lower, upper};
    result.calibrated = false;
    result.calibrationStatus = "UNCALIBRATED";
    result.valid = true;
    result.quality = DataQualityState::VALID;
    result.notes.push_back("heuristic band; not a calibrated probability");
    result.notes.push_back("confidence=" + std::to_string(confidence.confidence));
    return result;
}

}  // namespace aura
