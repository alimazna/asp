#pragma once
// AUD-0001 - Audit contract: the set of auditable action categories.

#include <string>

namespace aura {

enum class AuditAction {
    SERVICE_STARTED,
    SERVICE_STOPPED,
    SERVICE_STATE_CHANGED,
    SYSTEM_MODE_CHANGED,
    CONFIGURATION_CHANGED,
    DATA_INGESTED,
    DATA_QUALITY_CHANGED,
    DECISION_PRODUCED,
    RISK_PROPOSED,
    SHADOW_COMMAND_ISSUED,
    SHADOW_FILL_RECORDED,
    SHADOW_POSITION_CLOSED,
    OUTCOME_RECORDED,
    RECONCILIATION_RUN,
    RECOVERY_ACTION,
    APPROVAL_REQUESTED,
    APPROVAL_GRANTED,
    APPROVAL_DENIED,
    PROMOTION_ATTEMPTED,
    ROLLBACK_PERFORMED,
    ERROR_RAISED,
    UNKNOWN,
};

inline const char* toString(AuditAction a) noexcept {
    switch (a) {
        case AuditAction::SERVICE_STARTED:          return "SERVICE_STARTED";
        case AuditAction::SERVICE_STOPPED:          return "SERVICE_STOPPED";
        case AuditAction::SERVICE_STATE_CHANGED:    return "SERVICE_STATE_CHANGED";
        case AuditAction::SYSTEM_MODE_CHANGED:      return "SYSTEM_MODE_CHANGED";
        case AuditAction::CONFIGURATION_CHANGED:    return "CONFIGURATION_CHANGED";
        case AuditAction::DATA_INGESTED:            return "DATA_INGESTED";
        case AuditAction::DATA_QUALITY_CHANGED:     return "DATA_QUALITY_CHANGED";
        case AuditAction::DECISION_PRODUCED:        return "DECISION_PRODUCED";
        case AuditAction::RISK_PROPOSED:            return "RISK_PROPOSED";
        case AuditAction::SHADOW_COMMAND_ISSUED:    return "SHADOW_COMMAND_ISSUED";
        case AuditAction::SHADOW_FILL_RECORDED:     return "SHADOW_FILL_RECORDED";
        case AuditAction::SHADOW_POSITION_CLOSED:   return "SHADOW_POSITION_CLOSED";
        case AuditAction::OUTCOME_RECORDED:         return "OUTCOME_RECORDED";
        case AuditAction::RECONCILIATION_RUN:       return "RECONCILIATION_RUN";
        case AuditAction::RECOVERY_ACTION:          return "RECOVERY_ACTION";
        case AuditAction::APPROVAL_REQUESTED:       return "APPROVAL_REQUESTED";
        case AuditAction::APPROVAL_GRANTED:         return "APPROVAL_GRANTED";
        case AuditAction::APPROVAL_DENIED:          return "APPROVAL_DENIED";
        case AuditAction::PROMOTION_ATTEMPTED:      return "PROMOTION_ATTEMPTED";
        case AuditAction::ROLLBACK_PERFORMED:       return "ROLLBACK_PERFORMED";
        case AuditAction::ERROR_RAISED:             return "ERROR_RAISED";
        case AuditAction::UNKNOWN:                  return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // namespace aura
