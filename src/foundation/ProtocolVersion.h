#pragma once
// FND-0008 - Frozen foundational contract: bridge/API protocol version.

#include <cstdint>
#include <string>

namespace aura {

struct ProtocolVersion {
    std::int32_t major = 1;
    std::int32_t minor = 0;

    std::string toString() const {
        return std::to_string(major) + "." + std::to_string(minor);
    }

    bool operator==(const ProtocolVersion& o) const noexcept {
        return major == o.major && minor == o.minor;
    }
    bool operator!=(const ProtocolVersion& o) const noexcept { return !(*this == o); }

    // A peer is compatible when it shares our major line and is at least as
    // new on the minor line. Unknown majors are incompatible by definition.
    bool isCompatibleWith(const ProtocolVersion& required) const noexcept {
        return major == required.major && minor >= required.minor;
    }
};

}  // namespace aura
