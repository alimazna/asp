#pragma once
// CFG-0005 - Configuration contract: a recorded, auditable configuration change.
// Changes are never silent: they carry actor, time, and before/after versions.

#include "config/ConfigurationKey.h"
#include "config/ConfigurationScope.h"
#include "config/ConfigurationValue.h"
#include "foundation/Timestamp.h"
#include "foundation/Version.h"

#include <string>

namespace aura {

struct ConfigurationChange {
    ConfigurationKey key;
    ConfigurationScope scope = ConfigurationScope::GLOBAL;
    ConfigurationValue previous;
    ConfigurationValue current;
    Version fromVersion;
    Version toVersion;
    Timestamp changedAt;
    std::string actor;    // who/what requested the change
    std::string reason;
};

}  // namespace aura
