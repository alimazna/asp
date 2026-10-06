#pragma once
// RES-0032 - Resilience runtime: resolves failure impact across capabilities.

#include "resilience/CapabilityId.h"
#include "resilience/CriticalityPolicy.h"
#include "resilience/DegradationImpact.h"
#include "resilience/DependencyGraph.h"

#include <vector>

namespace aura {

class ImpactResolver {
public:
    ImpactResolver(const DependencyGraph& graph, const CriticalityPolicy& policy)
        : graph_(graph), policy_(policy) {}

    // Given a failed capability, compute impacts for it and all transitive
    // dependents. Hard edges BLOCK; soft edges DEGRADE. The failed capability
    // itself is always BLOCKED.
    std::vector<DegradationImpact> resolve(const CapabilityId& failed) const;

private:
    const DependencyGraph& graph_;
    const CriticalityPolicy& policy_;
};

}  // namespace aura
