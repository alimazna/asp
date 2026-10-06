#pragma once
// DEC-0008 - Deterministic regime engine.

#include "features/FeatureSnapshot.h"
#include "regime/RegimeState.h"
#include "structure/StructureSnapshot.h"

namespace aura {

struct RegimeConfig {
    double trendSlopeThreshold = 0.0008;   // |fast-slow|/slow
    double volatileThreshold = 0.012;      // log-return stdev
    double quietThreshold = 0.0015;
};

class RegimeEngine {
public:
    explicit RegimeEngine(RegimeConfig config = {}) : config_(config) {}

    RegimeState compute(const FeatureSnapshot& features,
                        const StructureSnapshot& structure) const;

private:
    RegimeConfig config_;
};

}  // namespace aura
