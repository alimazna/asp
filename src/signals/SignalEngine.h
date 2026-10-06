#pragma once
// DEC-0012 - Deterministic signal engine.
//
// Produces a directional candidate from features, structure, and regime, but
// only when eligibility permits. A NOT_ELIGIBLE state yields NO_SIGNAL; it
// never yields a fabricated direction.

#include "eligibility/EligibilityEngine.h"
#include "features/FeatureSnapshot.h"
#include "foundation/DataQualityState.h"
#include "regime/RegimeState.h"
#include "structure/StructureSnapshot.h"

#include <cstdint>
#include <string>

namespace aura {

enum class SignalDirection {
    LONG,
    SHORT,
    NONE,
};

inline const char* toString(SignalDirection d) noexcept {
    switch (d) {
        case SignalDirection::LONG:  return "LONG";
        case SignalDirection::SHORT: return "SHORT";
        case SignalDirection::NONE:  return "NONE";
    }
    return "NONE";
}

struct SignalCandidate {
    Timeframe timeframe = Timeframe::M15;
    std::int64_t asOfBarOpenSec = 0;
    SignalDirection direction = SignalDirection::NONE;
    double rawStrength = 0.0;     // 0..1 pre-score
    double referencePrice = 0.0;
    double suggestedStop = 0.0;
    double suggestedTarget = 0.0;
    std::string rationale;
    DataQualityState quality = DataQualityState::UNKNOWN;
    bool valid = false;
};

struct SignalConfig {
    double rsiLongThreshold = 52.0;
    double rsiShortThreshold = 48.0;
    double atrStopMultiplier = 1.5;
    double atrTargetMultiplier = 2.5;
};

class SignalEngine {
public:
    explicit SignalEngine(SignalConfig config = {}) : config_(config) {}

    SignalCandidate generate(const FeatureSnapshot& features,
                             const StructureSnapshot& structure,
                             const RegimeState& regime,
                             const EligibilityState& eligibility) const;

private:
    SignalConfig config_;
};

}  // namespace aura
