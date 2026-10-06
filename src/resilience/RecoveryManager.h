#pragma once
// RES-0026 - Resilience runtime: bounded recovery attempts.
//
// Recovery is policy-bounded: a bounded number of attempts per capability
// within a window. Exhaustion escalates rather than retrying forever.

#include "foundation/RecoveryAction.h"
#include "foundation/Timestamp.h"
#include "resilience/CapabilityId.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace aura {

struct RecoveryPolicy {
    int maxAttempts = 3;
    std::int64_t windowMillis = 300000;   // 5 minutes
    RecoveryAction action = RecoveryAction::RESTART_SUBSYSTEM;
};

enum class RecoveryDecision {
    ATTEMPT,
    EXHAUSTED,
};

class RecoveryManager {
public:
    RecoveryManager() = default;

    void setPolicy(const CapabilityId& id, const RecoveryPolicy& policy);

    // Request recovery. Returns ATTEMPT (with chosen action) or EXHAUSTED.
    RecoveryDecision request(const CapabilityId& id, Timestamp now,
                             RecoveryAction& outAction);

    // Called when a capability recovers; resets its attempt counter.
    void recordSuccess(const CapabilityId& id);

    int attempts(const CapabilityId& id, Timestamp now) const;

private:
    mutable std::mutex mutex_;
    std::map<CapabilityId, RecoveryPolicy> policies_;
    std::map<CapabilityId, std::vector<Timestamp>> attempts_;
};

}  // namespace aura
