// DEC-0011 - Eligibility gate implementation.

#include "eligibility/EligibilityEngine.h"

namespace aura {

EligibilityState EligibilityEngine::evaluate(const FeatureSnapshot& features,
                                             const RegimeState& regime,
                                             const StructureSnapshot& structure) const {
    EligibilityState state;

    if (!features.valid || !regime.valid) {
        state.decision = EligibilityDecision::UNKNOWN;
        state.quality = !isDecisionGrade(features.quality) ? features.quality
                                                           : regime.quality;
        state.reasons.push_back("features or regime not decision-grade");
        return state;
    }

    if (config_.requireValidStructure && !structure.valid) {
        state.decision = EligibilityDecision::UNKNOWN;
        state.quality = structure.quality;
        state.reasons.push_back("structure not decision-grade");
        return state;
    }

    bool eligible = true;
    switch (regime.regime) {
        case RegimeType::VOLATILE:
            if (!config_.allowVolatile) {
                eligible = false;
                state.reasons.push_back("volatile regime not permitted by policy");
            }
            break;
        case RegimeType::RANGING:
            if (!config_.allowRanging) {
                eligible = false;
                state.reasons.push_back("ranging regime not permitted by policy");
            }
            break;
        case RegimeType::UNKNOWN:
            eligible = false;
            state.reasons.push_back("unknown regime");
            break;
        default:
            break;
    }

    state.valid = true;
    state.quality = DataQualityState::VALID;
    state.decision = eligible ? EligibilityDecision::ELIGIBLE
                              : EligibilityDecision::NOT_ELIGIBLE;
    if (eligible) state.reasons.push_back("regime eligible");
    return state;
}

}  // namespace aura
