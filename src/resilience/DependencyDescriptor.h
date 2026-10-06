#pragma once
// RES-0004 - Resilience contract: a directed capability dependency edge.
// `dependent` cannot operate without `dependency`.

#include "resilience/CapabilityId.h"

namespace aura {

struct DependencyDescriptor {
    CapabilityId dependent;
    CapabilityId dependency;
    bool hard = true;   // hard: dependency failure blocks; soft: degrades only
};

}  // namespace aura
