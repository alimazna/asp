#pragma once
// RES-0003 - Resilience contract: describes an enableable capability.

#include "foundation/ServiceState.h"
#include "resilience/CapabilityId.h"

#include <string>

namespace aura {

struct CapabilityDescriptor {
    CapabilityId id;
    std::string name;
    std::string owningService;
    ServiceState initialState = ServiceState::STARTING;
    bool requiresFreshMarketData = false;
    bool affectsExecution = false;
};

}  // namespace aura
