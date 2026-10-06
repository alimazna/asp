#pragma once
// RES-0024 - Resilience runtime: applies degradation impacts to capabilities.
//
// Rule: disable only dependent capabilities; keep safe independent
// capabilities running. UNKNOWN/STALE data never keeps a dependent capability
// enabled.

#include "resilience/CapabilityId.h"
#include "resilience/DegradationImpact.h"

#include <map>
#include <mutex>
#include <vector>

namespace aura {

class GracefulDegradationManager {
public:
    GracefulDegradationManager() = default;

    // Apply a set of impacts. BLOCKED -> disabled; DEGRADED -> degraded.
    void applyImpacts(const std::vector<DegradationImpact>& impacts);

    // Clear impacts originating from a recovered capability.
    void clearCause(const CapabilityId& cause);

    bool isEnabled(const CapabilityId& id) const;   // default: enabled
    bool isDegraded(const CapabilityId& id) const;

    std::map<CapabilityId, ImpactLevel> activeImpacts() const;

private:
    mutable std::mutex mutex_;
    std::map<CapabilityId, DegradationImpact> active_;
};

}  // namespace aura
