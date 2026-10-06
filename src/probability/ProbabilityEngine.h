#pragma once
// DEC-0018 - Probability boundary engine.
//
// This engine produces bounded, explicitly UNCALIBRATED probability bands from
// the confidence and score. `calibrated` is always false until a real
// calibration procedure exists. Callers MUST NOT present `estimate` as a
// calibrated probability.

#include "confidence/ConfidenceEngine.h"
#include "foundation/DataQualityState.h"
#include "scoring/ScoreEngine.h"
#include "signals/SignalEngine.h"

#include <string>
#include <vector>

namespace aura {

struct ProbabilityBand {
    double estimate = 0.0;   // point estimate (uncalibrated)
    double lower = 0.0;      // lower boundary
    double upper = 0.0;      // upper boundary
};

struct ProbabilityResult {
    ProbabilityBand band;
    bool calibrated = false; // structurally false until calibration exists
    std::string calibrationStatus = "UNCALIBRATED";
    std::vector<std::string> notes;
    DataQualityState quality = DataQualityState::UNKNOWN;
    bool valid = false;
};

class ProbabilityEngine {
public:
    ProbabilityEngine() = default;

    ProbabilityResult compute(const ScoreResult& score,
                              const ConfidenceResult& confidence,
                              const SignalCandidate& candidate) const;
};

}  // namespace aura
