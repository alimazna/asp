#pragma once
// GDN-0001 - Guardian contract: the Guardian's protective state.
// Guardian authority may only restrict; it can never grant new authority.

#include <string>

namespace aura {

enum class GuardianStatus {
    NORMAL,     // policy enforced, no protective clamp active
    SAFE_MODE,  // restrictive: only safe independent capabilities run
    HALTED,     // all execution-affecting capability stopped
    FROZEN,     // policy itself locked; no change accepted
};

inline const char* toString(GuardianStatus s) noexcept {
    switch (s) {
        case GuardianStatus::NORMAL:    return "NORMAL";
        case GuardianStatus::SAFE_MODE: return "SAFE_MODE";
        case GuardianStatus::HALTED:    return "HALTED";
        case GuardianStatus::FROZEN:    return "FROZEN";
    }
    return "UNKNOWN";
}

}  // namespace aura
