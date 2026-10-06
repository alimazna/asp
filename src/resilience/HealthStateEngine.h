#pragma once
// RES-0016 - Resilience runtime: derives aggregate health from observations.

#include "foundation/EntityId.h"
#include "foundation/ServiceState.h"
#include "resilience/HealthSnapshot.h"

#include <map>
#include <mutex>
#include <vector>

namespace aura {

class HealthStateEngine {
public:
    HealthStateEngine() = default;

    void observe(const HealthSnapshot& snapshot);

    bool latest(const EntityId& serviceId, HealthSnapshot& out) const;
    std::vector<HealthSnapshot> snapshots() const;

    // Most-severe state across all observed services.
    ServiceState aggregateState() const;

    // Severity ordering helper (higher = worse).
    static int severityRank(ServiceState state) noexcept;
    static ServiceState combine(ServiceState a, ServiceState b) noexcept;

private:
    mutable std::mutex mutex_;
    std::map<EntityId, HealthSnapshot> latest_;
};

}  // namespace aura
