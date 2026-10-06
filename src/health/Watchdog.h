#pragma once
// SHD-0017 - Bridge watchdog.
//
// Periodically probes the bridge; on consecutive failures it requests a
// bounded recovery through the supervisor. It never spawns an unbounded
// restart loop and it never masks a persistent failure.

#include "platform/windows/ProcessSupervisor.h"
#include "mt5/PythonBridgeClient.h"
#include "foundation/Timestamp.h"

#include <memory>
#include <string>

namespace aura {

struct WatchdogPolicy {
    int failureThreshold = 3;         // consecutive failures before recovery
    int probeIntervalMillis = 5000;
};

enum class WatchdogAction {
    NONE,
    RECOVERED,
    RECOVERY_REQUESTED,
    RECOVERY_EXHAUSTED,
};

inline const char* toString(WatchdogAction a) noexcept {
    switch (a) {
        case WatchdogAction::NONE:               return "NONE";
        case WatchdogAction::RECOVERED:          return "RECOVERED";
        case WatchdogAction::RECOVERY_REQUESTED: return "RECOVERY_REQUESTED";
        case WatchdogAction::RECOVERY_EXHAUSTED: return "RECOVERY_EXHAUSTED";
    }
    return "NONE";
}

class Watchdog {
public:
    Watchdog(std::shared_ptr<IPythonBridgeClient> client,
             ProcessSupervisor* supervisor,
             WatchdogPolicy policy = {});

    // Run a single probe cycle. Returns the action taken.
    WatchdogAction probe(Timestamp now);

    int consecutiveFailures() const noexcept { return consecutiveFailures_; }
    const std::string& lastReason() const noexcept { return lastReason_; }
    const WatchdogPolicy& policy() const noexcept { return policy_; }

private:
    std::shared_ptr<IPythonBridgeClient> client_;
    ProcessSupervisor* supervisor_;
    WatchdogPolicy policy_;
    int consecutiveFailures_ = 0;
    std::string lastReason_;
};

}  // namespace aura
