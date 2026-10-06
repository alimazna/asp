#pragma once
// CFG-0004 - Configuration contract: the authority scope a key belongs to.

#include <string>

namespace aura {

enum class ConfigurationScope {
    GLOBAL,
    RUNTIME,
    BRIDGE,
    DATA,
    DECISION,
    RISK,
    SHADOW,
    RESEARCH,
    GOVERNANCE,
    TELEGRAM,
};

inline const char* toString(ConfigurationScope s) noexcept {
    switch (s) {
        case ConfigurationScope::GLOBAL:     return "GLOBAL";
        case ConfigurationScope::RUNTIME:    return "RUNTIME";
        case ConfigurationScope::BRIDGE:     return "BRIDGE";
        case ConfigurationScope::DATA:       return "DATA";
        case ConfigurationScope::DECISION:   return "DECISION";
        case ConfigurationScope::RISK:       return "RISK";
        case ConfigurationScope::SHADOW:     return "SHADOW";
        case ConfigurationScope::RESEARCH:   return "RESEARCH";
        case ConfigurationScope::GOVERNANCE: return "GOVERNANCE";
        case ConfigurationScope::TELEGRAM:   return "TELEGRAM";
    }
    return "UNKNOWN";
}

}  // namespace aura
