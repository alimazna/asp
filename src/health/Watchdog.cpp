// SHD-0018 - Watchdog implementation.

#include "health/Watchdog.h"

namespace aura {

Watchdog::Watchdog(std::shared_ptr<IPythonBridgeClient> client,
                   ProcessSupervisor* supervisor, WatchdogPolicy policy)
    : client_(std::move(client)), supervisor_(supervisor), policy_(policy) {}

WatchdogAction Watchdog::probe(Timestamp now) {
    (void)now;
    if (!client_) {
        lastReason_ = "no bridge client";
        ++consecutiveFailures_;
        return WatchdogAction::NONE;
    }

    const auto health = client_->health();
    if (health.ok && health.value.initialized) {
        const bool wasFailing = consecutiveFailures_ > 0;
        consecutiveFailures_ = 0;
        lastReason_.clear();
        if (supervisor_ != nullptr) supervisor_->markHealthy();
        return wasFailing ? WatchdogAction::RECOVERED : WatchdogAction::NONE;
    }

    ++consecutiveFailures_;
    lastReason_ = health.ok ? "bridge not initialized" : health.error.message;
    if (supervisor_ != nullptr) supervisor_->markUnhealthy(lastReason_);

    if (consecutiveFailures_ < policy_.failureThreshold) {
        return WatchdogAction::NONE;
    }

    if (supervisor_ == nullptr) {
        return WatchdogAction::RECOVERY_EXHAUSTED;
    }

    std::string error;
    if (supervisor_->restart(error)) {
        consecutiveFailures_ = 0;
        return WatchdogAction::RECOVERY_REQUESTED;
    }
    lastReason_ = error;
    return WatchdogAction::RECOVERY_EXHAUSTED;
}

}  // namespace aura
