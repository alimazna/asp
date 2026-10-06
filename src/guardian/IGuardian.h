#pragma once
// GDN-0003 - Guardian contract: the authority-evaluation interface.

#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"
#include "guardian/GuardianPolicy.h"
#include "guardian/GuardianStatus.h"

#include <string>

namespace aura {

enum class GuardianDecision {
    ALLOW,
    DENY,
    REQUIRE_APPROVAL,
};

inline const char* toString(GuardianDecision d) noexcept {
    switch (d) {
        case GuardianDecision::ALLOW:            return "ALLOW";
        case GuardianDecision::DENY:             return "DENY";
        case GuardianDecision::REQUIRE_APPROVAL: return "REQUIRE_APPROVAL";
    }
    return "DENY";
}

struct GuardianRequest {
    std::string action;       // e.g. "shadow.execute", "live.execute"
    std::string actor;
    bool affectsExecution = false;
    bool isLiveExecution = false;
    EntityId correlationId;
};

struct GuardianVerdict {
    GuardianDecision decision = GuardianDecision::DENY;
    GuardianStatus status = GuardianStatus::NORMAL;
    std::string reason;
    Timestamp evaluatedAt;
};

class IGuardian {
public:
    virtual ~IGuardian() = default;

    virtual GuardianStatus status() const noexcept = 0;
    virtual GuardianPolicy policy() const = 0;
    virtual GuardianVerdict evaluate(const GuardianRequest& request) const = 0;

    // Protection controls. These may only move the system to a more
    // restrictive state; they cannot relax restrictions.
    virtual bool engageSafeMode(const std::string& reason) = 0;
    virtual bool halt(const std::string& reason) = 0;
    virtual bool freeze() = 0;

    // A policy update is accepted only if it remains valid (never grants live
    // authority) and the Guardian is not FROZEN.
    virtual bool applyPolicy(const GuardianPolicy& policy) = 0;
};

}  // namespace aura
