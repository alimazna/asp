#pragma once
// FND-0009 - Frozen foundational contract: serialization schema version.

#include <cstdint>
#include <string>

namespace aura {

struct SchemaVersion {
    std::int32_t major = 1;
    std::int32_t minor = 0;

    std::string toString() const {
        return std::to_string(major) + "." + std::to_string(minor);
    }

    bool operator==(const SchemaVersion& o) const noexcept {
        return major == o.major && minor == o.minor;
    }
    bool operator!=(const SchemaVersion& o) const noexcept { return !(*this == o); }

    bool isCompatibleWith(const SchemaVersion& required) const noexcept {
        return major == required.major && minor >= required.minor;
    }
};

}  // namespace aura
