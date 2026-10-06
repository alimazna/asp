#pragma once
// CFG-0003 - Configuration contract: an immutable, versioned configuration set.
// A snapshot is the unit of reproducibility: decisions reference its version.

#include "config/ConfigurationKey.h"
#include "config/ConfigurationScope.h"
#include "config/ConfigurationValue.h"
#include "foundation/HashDigest.h"
#include "foundation/Timestamp.h"
#include "foundation/Version.h"

#include <map>
#include <string>

namespace aura {

struct ConfigurationSnapshot {
    Version version;
    Timestamp capturedAt;
    HashDigest digest;
    std::map<ConfigurationKey, ConfigurationValue> entries;

    bool has(const ConfigurationKey& key) const { return entries.count(key) != 0; }

    const ConfigurationValue* find(const ConfigurationKey& key) const {
        auto it = entries.find(key);
        return it == entries.end() ? nullptr : &it->second;
    }
};

}  // namespace aura
