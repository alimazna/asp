#pragma once
// FND-0003 - Frozen foundational contract: semantic version triple.

#include <cstdint>
#include <string>

namespace aura {

struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    std::string toString() const {
        return std::to_string(major) + "." + std::to_string(minor) + "." +
               std::to_string(patch);
    }

    bool operator==(const Version& o) const noexcept {
        return major == o.major && minor == o.minor && patch == o.patch;
    }
    bool operator!=(const Version& o) const noexcept { return !(*this == o); }
    bool operator<(const Version& o) const noexcept {
        if (major != o.major) return major < o.major;
        if (minor != o.minor) return minor < o.minor;
        return patch < o.patch;
    }
    bool operator>=(const Version& o) const noexcept { return !(*this < o); }
};

}  // namespace aura
