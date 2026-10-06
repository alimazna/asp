// RES-0033 - Resilience runtime: impact resolver implementation.

#include "resilience/ImpactResolver.h"

#include <map>
#include <set>

namespace aura {

std::vector<DegradationImpact> ImpactResolver::resolve(
    const CapabilityId& failed) const {
    std::vector<DegradationImpact> impacts;
    if (failed.empty()) return impacts;

    DegradationImpact self;
    self.capability = failed;
    self.level = ImpactLevel::BLOCKED;
    self.cause = failed;
    self.reason = "capability failed";
    impacts.push_back(self);

    // Worst-level wins per capability.
    std::map<CapabilityId, ImpactLevel> worst;
    worst[failed] = ImpactLevel::BLOCKED;

    for (const auto& dependent : graph_.allDependents(failed)) {
        ImpactLevel level = ImpactLevel::BLOCKED;
        std::string reason = "hard dependency failed";
        for (const auto& edge : graph_.dependenciesOf(dependent)) {
            if (edge.dependency == failed && !edge.hard) {
                level = ImpactLevel::DEGRADED;
                reason = "soft dependency failed";
            }
        }
        // If any dependency in the failed set is hard, block. We approximate
        // by consulting the direct edge to the failed capability, then
        // escalate if the criticality of the failed capability is CRITICAL.
        if (policy_.of(failed) == Criticality::CRITICAL) {
            level = ImpactLevel::BLOCKED;
            reason = "critical capability failed";
        }
        auto it = worst.find(dependent);
        if (it == worst.end() || static_cast<int>(level) > static_cast<int>(it->second)) {
            worst[dependent] = level;
        }
    }

    for (const auto& kv : worst) {
        if (kv.first == failed) continue;
        DegradationImpact impact;
        impact.capability = kv.first;
        impact.level = kv.second;
        impact.cause = failed;
        impact.reason = (kv.second == ImpactLevel::BLOCKED)
                            ? "dependency failure blocks capability"
                            : "dependency failure degrades capability";
        impacts.push_back(impact);
    }
    return impacts;
}

}  // namespace aura
