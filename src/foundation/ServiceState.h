#pragma once
// FND-0004 - Frozen foundational contract: service (subsystem) states.
// Service state is distinct from system operating mode (SystemMode.h).

#include <string>

namespace aura {

enum class ServiceState {
    STARTING,
    ONLINE,
    DEGRADED,
    OFFLINE,
    RECOVERING,
    PAUSED,
    BLOCKED,
    ERROR,
};

inline const char* toString(ServiceState s) noexcept {
    switch (s) {
        case ServiceState::STARTING:   return "STARTING";
        case ServiceState::ONLINE:     return "ONLINE";
        case ServiceState::DEGRADED:   return "DEGRADED";
        case ServiceState::OFFLINE:    return "OFFLINE";
        case ServiceState::RECOVERING: return "RECOVERING";
        case ServiceState::PAUSED:     return "PAUSED";
        case ServiceState::BLOCKED:    return "BLOCKED";
        case ServiceState::ERROR:      return "ERROR";
    }
    return "UNKNOWN";
}

inline bool parseServiceState(const std::string& text, ServiceState& out) noexcept {
    if (text == "STARTING")   { out = ServiceState::STARTING;   return true; }
    if (text == "ONLINE")     { out = ServiceState::ONLINE;     return true; }
    if (text == "DEGRADED")   { out = ServiceState::DEGRADED;   return true; }
    if (text == "OFFLINE")    { out = ServiceState::OFFLINE;    return true; }
    if (text == "RECOVERING") { out = ServiceState::RECOVERING; return true; }
    if (text == "PAUSED")     { out = ServiceState::PAUSED;     return true; }
    if (text == "BLOCKED")    { out = ServiceState::BLOCKED;    return true; }
    if (text == "ERROR")      { out = ServiceState::ERROR;      return true; }
    return false;
}

}  // namespace aura
