#pragma once
// DEC-0014 - Deterministic score engine.
//
// The score is a bounded, reproducible ranking value in [0,100]. It is NOT a
// probability and must never be presented as one. It combines only inputs that
// are themselves decision-grade.

#include "features/FeatureSnapshot.h"
#include "foundation/DataQualityState.h"
#include "regime/RegimeState.h"
#include "signals/SignalEngine.h"
#include "structure/StructureSnapshot.h"

#include <string>
#include <vector>

namespace aura {

struct ScoreBreakdown {
    double directionScore = 0.0;
    double structureScore = 0.0;
    double regimeScore = 0.0;
    double momentumScore = 0.0;
    double volatilityPenalty = 0.0;
};

struct ScoreResult {
    double score = 0.0;            // 0..100
    ScoreBreakdown breakdown;
    std::vector<std::string> components;
    DataQualityState quality = DataQualityState::UNKNOWN;
    bool valid = false;
};

class ScoreEngine {
public:
    ScoreEngine() = default;

    ScoreResult compute(const FeatureSnapshot& features,
                        const StructureSnapshot& structure,
                        const RegimeState& regime,
                        const SignalCandidate& candidate) const;
};

}  // namespace aura
