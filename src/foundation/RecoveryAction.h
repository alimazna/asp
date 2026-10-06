#pragma once
// FND-0017 - Frozen foundational contract: bounded recovery actions.

#include <string>

namespace aura {

enum class RecoveryAction {
    NONE,
    RETRY,
    RESTART_SUBSYSTEM,
    RESTART_BRIDGE,
    RELOAD_CONFIGURATION,
    ROLLBACK_TO_CHECKPOINT,
    ISOLATE,
    PAUSE,
    RESUME,
    ESCALATE_TO_HUMAN,
    HALT,
};

inline const char* toString(RecoveryAction a) noexcept {
    switch (a) {
        case RecoveryAction::NONE:                   return "NONE";
        case RecoveryAction::RETRY:                  return "RETRY";
        case RecoveryAction::RESTART_SUBSYSTEM:      return "RESTART_SUBSYSTEM";
        case RecoveryAction::RESTART_BRIDGE:         return "RESTART_BRIDGE";
        case RecoveryAction::RELOAD_CONFIGURATION:   return "RELOAD_CONFIGURATION";
        case RecoveryAction::ROLLBACK_TO_CHECKPOINT: return "ROLLBACK_TO_CHECKPOINT";
        case RecoveryAction::ISOLATE:                return "ISOLATE";
        case RecoveryAction::PAUSE:                  return "PAUSE";
        case RecoveryAction::RESUME:                 return "RESUME";
        case RecoveryAction::ESCALATE_TO_HUMAN:      return "ESCALATE_TO_HUMAN";
        case RecoveryAction::HALT:                   return "HALT";
    }
    return "NONE";
}

}  // namespace aura
