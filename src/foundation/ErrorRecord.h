#pragma once
// FND-0016 - Frozen foundational contract: a structured error record.
//
// Every failure surfaced by the backend is recorded explicitly with its
// severity, code, service context, and a bounded recovery action. Errors are
// never silently swallowed.

#include "foundation/ErrorCode.h"
#include "foundation/ErrorSeverity.h"
#include "foundation/RecoveryAction.h"
#include "foundation/ServiceState.h"
#include "foundation/Timestamp.h"

#include <string>
#include <utility>

namespace aura {

struct ErrorRecord {
    ErrorCode code = ErrorCode::NONE;
    ErrorSeverity severity = ErrorSeverity::ERROR;
    ServiceState serviceState = ServiceState::ERROR;
    RecoveryAction recovery = RecoveryAction::NONE;
    std::string message;
    std::string context;
    Timestamp occurredAt;

    bool isError() const noexcept { return code != ErrorCode::NONE; }

    static ErrorRecord make(ErrorCode code, ErrorSeverity severity,
                            std::string message, std::string context = {}) {
        ErrorRecord r;
        r.code = code;
        r.severity = severity;
        r.message = std::move(message);
        r.context = std::move(context);
        r.occurredAt = Timestamp::now();
        return r;
    }
};

}  // namespace aura
