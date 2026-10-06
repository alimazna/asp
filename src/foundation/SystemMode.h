#pragma once
// FND-0005 - Frozen foundational contract: system operating modes.
// Operating mode is distinct from per-service ServiceState.

#include <string>

namespace aura {

enum class SystemMode {
    STARTING,
    RECOVERY,
    NORMAL,
    DEGRADED,
    SHADOW,
    PAUSED,
    MANUAL,
    EMERGENCY,
    HALTED,
};

inline const char* toString(SystemMode m) noexcept {
    switch (m) {
        case SystemMode::STARTING:  return "STARTING";
        case SystemMode::RECOVERY:  return "RECOVERY";
        case SystemMode::NORMAL:    return "NORMAL";
        case SystemMode::DEGRADED:  return "DEGRADED";
        case SystemMode::SHADOW:    return "SHADOW";
        case SystemMode::PAUSED:    return "PAUSED";
        case SystemMode::MANUAL:    return "MANUAL";
        case SystemMode::EMERGENCY: return "EMERGENCY";
        case SystemMode::HALTED:    return "HALTED";
    }
    return "UNKNOWN";
}

inline bool parseSystemMode(const std::string& text, SystemMode& out) noexcept {
    if (text == "STARTING")  { out = SystemMode::STARTING;  return true; }
    if (text == "RECOVERY")  { out = SystemMode::RECOVERY;  return true; }
    if (text == "NORMAL")    { out = SystemMode::NORMAL;    return true; }
    if (text == "DEGRADED")  { out = SystemMode::DEGRADED;  return true; }
    if (text == "SHADOW")    { out = SystemMode::SHADOW;    return true; }
    if (text == "PAUSED")    { out = SystemMode::PAUSED;    return true; }
    if (text == "MANUAL")    { out = SystemMode::MANUAL;    return true; }
    if (text == "EMERGENCY") { out = SystemMode::EMERGENCY; return true; }
    if (text == "HALTED")    { out = SystemMode::HALTED;    return true; }
    return false;
}

}  // namespace aura
