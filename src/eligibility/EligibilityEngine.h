#pragma once
// DEC-0010 - Deterministic eligibility gate.
//
// Eligibility answers "may this market state be considered at all?" before any
// signal is produced. It is a gate, not a signal: it carries no direction.

#include "features/FeatureSnapshot.h"
#include "foundation/DataQualityState.h"
#include "regime/RegimeState.h"
#include "structure/StructureSnapshot.h"

#include <string>
#include <vector>

namespace aura {

enum class EligibilityDecision {
    ELIGIBLE,
    NOT_ELIGIBLE,
    UNKNOWN,
};

inline const char* toString(EligibilityDecision d) noexcept {
    switch (d) {
        case EligibilityDecision::ELIGIBLE:     return "ELIGIBLE";
        case EligibilityDecision::NOT_ELIGIBLE: return "NOT_ELIGIBLE";
        case EligibilityDecision::UNKNOWN:      return "UNKNOWN";
    }
    return "UNKNOWN";
}

struct EligibilityState {
    EligibilityDecision decision = EligibilityDecision::UNKNOWN;
    DataQualityState quality = DataQualityState::UNKNOWN;
    std::vector<std::string> reasons;
    bool valid = false;
};

struct EligibilityConfig {
    bool allowRanging = true;
    bool allowVolatile = false;
    bool requireValidStructure = true;
};

class EligibilityEngine {
public:
    explicit EligibilityEngine(EligibilityConfig config = {}) : config_(config) {}

    EligibilityState evaluate(const FeatureSnapshot& features,
                              const RegimeState& regime,
                              const StructureSnapshot& structure) const;

private:
    EligibilityConfig config_;
};

}  // namespace aura
