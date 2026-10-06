#pragma once
// RES-0014 - Resilience runtime: directed capability dependency graph.
//
// Edges are dependent -> dependency. The graph answers "what breaks if X
// fails?" (transitive dependents) and "what does X need?" (dependencies).

#include "resilience/CapabilityId.h"
#include "resilience/DependencyDescriptor.h"

#include <cstddef>
#include <map>
#include <mutex>
#include <vector>

namespace aura {

class DependencyGraph {
public:
    DependencyGraph() = default;

    bool addDependency(const DependencyDescriptor& edge);

    bool hasEdge(const CapabilityId& dependent,
                 const CapabilityId& dependency) const;

    std::vector<DependencyDescriptor> dependenciesOf(const CapabilityId& id) const;
    std::vector<CapabilityId> directDependents(const CapabilityId& id) const;

    // All capabilities that transitively depend on `id` (excluding `id`).
    std::vector<CapabilityId> allDependents(const CapabilityId& id) const;

    // True if following dependency edges from any node can return to it.
    bool hasCycle() const;

    std::size_t edgeCount() const;
    std::vector<CapabilityId> nodes() const;

private:
    mutable std::mutex mutex_;
    std::map<CapabilityId, std::vector<DependencyDescriptor>> forward_;
    std::map<CapabilityId, std::vector<CapabilityId>> reverse_;
};

}  // namespace aura
