#pragma once
// FND-0012 - Frozen foundational contract: canonical event categories.
// The list is intentionally closed; unknown producers must not invent types.

#include <string>

namespace aura {

enum class EventType {
    DATA_INGESTED,
    DATA_QUALITY_CHANGED,
    TIMEFRAME_STATE_UPDATED,
    DECISION_PRODUCED,
    SIGNAL_EMITTED,
    RISK_PROPOSED,
    SHADOW_COMMAND_ISSUED,
    SHADOW_FILL_RECORDED,
    SHADOW_POSITION_OPENED,
    SHADOW_POSITION_CLOSED,
    OUTCOME_RECORDED,
    RECONCILIATION_RUN,
    SERVICE_STATE_CHANGED,
    SYSTEM_MODE_CHANGED,
    ERROR_RAISED,
    RECOVERY_ACTION_TAKEN,
    CONFIGURATION_CHANGED,
    RESEARCH_ACTION,
    GOVERNANCE_ACTION,
    UNKNOWN,
};

inline const char* toString(EventType t) noexcept {
    switch (t) {
        case EventType::DATA_INGESTED:           return "DATA_INGESTED";
        case EventType::DATA_QUALITY_CHANGED:    return "DATA_QUALITY_CHANGED";
        case EventType::TIMEFRAME_STATE_UPDATED: return "TIMEFRAME_STATE_UPDATED";
        case EventType::DECISION_PRODUCED:       return "DECISION_PRODUCED";
        case EventType::SIGNAL_EMITTED:          return "SIGNAL_EMITTED";
        case EventType::RISK_PROPOSED:           return "RISK_PROPOSED";
        case EventType::SHADOW_COMMAND_ISSUED:   return "SHADOW_COMMAND_ISSUED";
        case EventType::SHADOW_FILL_RECORDED:    return "SHADOW_FILL_RECORDED";
        case EventType::SHADOW_POSITION_OPENED:  return "SHADOW_POSITION_OPENED";
        case EventType::SHADOW_POSITION_CLOSED:  return "SHADOW_POSITION_CLOSED";
        case EventType::OUTCOME_RECORDED:        return "OUTCOME_RECORDED";
        case EventType::RECONCILIATION_RUN:      return "RECONCILIATION_RUN";
        case EventType::SERVICE_STATE_CHANGED:   return "SERVICE_STATE_CHANGED";
        case EventType::SYSTEM_MODE_CHANGED:     return "SYSTEM_MODE_CHANGED";
        case EventType::ERROR_RAISED:            return "ERROR_RAISED";
        case EventType::RECOVERY_ACTION_TAKEN:   return "RECOVERY_ACTION_TAKEN";
        case EventType::CONFIGURATION_CHANGED:   return "CONFIGURATION_CHANGED";
        case EventType::RESEARCH_ACTION:         return "RESEARCH_ACTION";
        case EventType::GOVERNANCE_ACTION:       return "GOVERNANCE_ACTION";
        case EventType::UNKNOWN:                 return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // namespace aura
