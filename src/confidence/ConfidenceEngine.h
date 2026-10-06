#pragma once
// DEC-0016 - Deterministic confidence engine.
//
// Confidence is a bounded [0,1] measure of how well-supported a decision is.
// It is a meta-measure, not a probability, and it is penalized by any data
// quality degradation anywhere in the chain.

#include "foundation/DataQualityState.h"
#include "regime/RegimeState.h"
#include "scoring/ScoreEngine.h"
#include "signals/SignalEngine.h"
#include "structure/StructureSnapshot.h"

#include <vector>

namespace aura {

struct ConfidenceResult {
    double confidence = 0.0;    // 0..1
    std::vector<std::string> factors;
    DataQualityState quality = DataQualityState::UNKNOWN;
    bool valid = false;
};

class ConfidenceEngine {
public:
    ConfidenceEngine() = default;

    ConfidenceResult compute(const ScoreResult& score,
                             const StructureSnapshot& structure,
                             const RegimeState& regime,
                             const SignalCandidate& candidate,
                             DataQualityState inputQuality) const;
};

}  // namespace aura
