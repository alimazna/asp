#pragma once
// AUD-0002 - Audit contract: the result of an audited action.

#include <string>

namespace aura {

enum class AuditOutcome {
    SUCCESS,
    FAILURE,
    REJECTED,
    PARTIAL,
    NO_OP,
};

inline const char* toString(AuditOutcome o) noexcept {
    switch (o) {
        case AuditOutcome::SUCCESS:  return "SUCCESS";
        case AuditOutcome::FAILURE:  return "FAILURE";
        case AuditOutcome::REJECTED: return "REJECTED";
        case AuditOutcome::PARTIAL:  return "PARTIAL";
        case AuditOutcome::NO_OP:    return "NO_OP";
    }
    return "UNKNOWN";
}

}  // namespace aura
