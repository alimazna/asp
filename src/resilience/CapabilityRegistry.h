#pragma once
// RES-0012 - Resilience runtime: registry of known capabilities.

#include "foundation/ServiceState.h"
#include "resilience/CapabilityDescriptor.h"
#include "resilience/CapabilityId.h"

#include <map>
#include <mutex>
#include <vector>

namespace aura {

class CapabilityRegistry {
public:
    CapabilityRegistry() = default;

    // Registration is idempotent-conflict: re-registering an existing id fails
    // rather than silently overwriting its identity.
    bool registerCapability(const CapabilityDescriptor& descriptor);

    bool updateState(const CapabilityId& id, ServiceState state);

    bool find(const CapabilityId& id, CapabilityDescriptor& out) const;
    bool contains(const CapabilityId& id) const;
    ServiceState stateOf(const CapabilityId& id) const;

    std::vector<CapabilityDescriptor> all() const;
    std::vector<CapabilityId> ids() const;
    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::map<CapabilityId, CapabilityDescriptor> capabilities_;
};

}  // namespace aura
