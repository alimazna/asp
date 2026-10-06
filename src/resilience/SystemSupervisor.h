#pragma once
// RES-0010 - Resilience runtime: coordinates subsystem supervision.
//
// The supervisor is the single point that records service state transitions
// and derives the aggregate system mode. It never hides a failure.

#include "foundation/ServiceState.h"
#include "foundation/SystemMode.h"
#include "foundation/Timestamp.h"
#include "resilience/HealthStateEngine.h"
#include "resilience/ServiceDescriptor.h"

#include <map>
#include <mutex>
#include <vector>

namespace aura {

class SystemSupervisor {
public:
    SystemSupervisor() = default;

    void registerService(const ServiceDescriptor& descriptor);
    bool setServiceState(const EntityId& serviceId, ServiceState state,
                         const std::string& detail = {});

    bool serviceState(const EntityId& serviceId, ServiceState& out) const;
    std::vector<ServiceDescriptor> services() const;

    // Aggregate operating mode derived from all registered service states.
    SystemMode deriveSystemMode() const;

    HealthStateEngine& health() noexcept { return health_; }
    const HealthStateEngine& health() const noexcept { return health_; }

private:
    mutable std::mutex mutex_;
    std::map<EntityId, ServiceDescriptor> services_;
    std::map<EntityId, ServiceState> states_;
    HealthStateEngine health_;
};

}  // namespace aura
