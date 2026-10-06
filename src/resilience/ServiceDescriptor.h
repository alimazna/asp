#pragma once
// RES-0001 - Resilience contract: describes a supervised service/subsystem.

#include "foundation/EntityId.h"
#include "foundation/ServiceState.h"
#include "foundation/Version.h"

#include <string>

namespace aura {

struct ServiceDescriptor {
    EntityId id;
    std::string name;
    Version version;
    bool critical = false;   // critical services gate whole-system availability
    bool ownsMarketData = false;
    std::string description;
};

}  // namespace aura
