#pragma once
// RES-0007 - Resilience contract: the impact of a failure on a capability.

#include "resilience/CapabilityId.h"

#include <string>

namespace aura {

enum class ImpactLevel {
    NONE,      // unaffected
    DEGRADED,  // can run with reduced confidence/coverage
    BLOCKED,   // must not run
};

inline const char* toString(ImpactLevel l) noexcept {
    switch (l) {
        case ImpactLevel::NONE:     return "NONE";
        case ImpactLevel::DEGRADED: return "DEGRADED";
        case ImpactLevel::BLOCKED:  return "BLOCKED";
    }
    return "BLOCKED";
}

struct DegradationImpact {
    CapabilityId capability;
    ImpactLevel level = ImpactLevel::NONE;
    CapabilityId cause;      // originating failed capability (may be empty)
    std::string reason;
};

}  // namespace aura
