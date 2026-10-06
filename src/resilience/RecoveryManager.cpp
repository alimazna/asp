// RES-0027 - Resilience runtime: recovery manager implementation.

#include "resilience/RecoveryManager.h"

#include <algorithm>
#include <vector>

namespace aura {

void RecoveryManager::setPolicy(const CapabilityId& id,
                                const RecoveryPolicy& policy) {
    if (id.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    policies_[id] = policy;
}

RecoveryDecision RecoveryManager::request(const CapabilityId& id, Timestamp now,
                                          RecoveryAction& outAction) {
    std::lock_guard<std::mutex> lock(mutex_);
    RecoveryPolicy policy;
    auto pit = policies_.find(id);
    if (pit != policies_.end()) policy = pit->second;

    auto& log = attempts_[id];
    const std::int64_t cutoff = now.epochMillis() - policy.windowMillis;
    log.erase(std::remove_if(log.begin(), log.end(),
                             [cutoff](const Timestamp& t) {
                                 return t.isUnknown() || t.epochMillis() < cutoff;
                             }),
              log.end());

    if (static_cast<int>(log.size()) >= policy.maxAttempts) {
        outAction = RecoveryAction::ESCALATE_TO_HUMAN;
        return RecoveryDecision::EXHAUSTED;
    }
    log.push_back(now);
    outAction = policy.action;
    return RecoveryDecision::ATTEMPT;
}

void RecoveryManager::recordSuccess(const CapabilityId& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    attempts_.erase(id);
}

int RecoveryManager::attempts(const CapabilityId& id, Timestamp now) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = attempts_.find(id);
    if (it == attempts_.end()) return 0;
    RecoveryPolicy policy;
    auto pit = policies_.find(id);
    if (pit != policies_.end()) policy = pit->second;
    const std::int64_t cutoff = now.epochMillis() - policy.windowMillis;
    int count = 0;
    for (const auto& t : it->second) {
        if (t.isKnown() && t.epochMillis() >= cutoff) ++count;
    }
    return count;
}

}  // namespace aura
