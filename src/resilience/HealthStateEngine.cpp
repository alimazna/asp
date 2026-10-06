// RES-0017 - Resilience runtime: health state engine implementation.

#include "resilience/HealthStateEngine.h"

namespace aura {

int HealthStateEngine::severityRank(ServiceState state) noexcept {
    switch (state) {
        case ServiceState::ONLINE:     return 0;
        case ServiceState::STARTING:   return 1;
        case ServiceState::PAUSED:     return 2;
        case ServiceState::RECOVERING: return 3;
        case ServiceState::DEGRADED:   return 4;
        case ServiceState::BLOCKED:    return 5;
        case ServiceState::OFFLINE:    return 6;
        case ServiceState::ERROR:      return 7;
    }
    return 7;
}

ServiceState HealthStateEngine::combine(ServiceState a, ServiceState b) noexcept {
    return severityRank(a) >= severityRank(b) ? a : b;
}

void HealthStateEngine::observe(const HealthSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_[snapshot.serviceId] = snapshot;
}

bool HealthStateEngine::latest(const EntityId& serviceId,
                               HealthSnapshot& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = latest_.find(serviceId);
    if (it == latest_.end()) return false;
    out = it->second;
    return true;
}

std::vector<HealthSnapshot> HealthStateEngine::snapshots() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<HealthSnapshot> out;
    out.reserve(latest_.size());
    for (const auto& kv : latest_) out.push_back(kv.second);
    return out;
}

ServiceState HealthStateEngine::aggregateState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (latest_.empty()) return ServiceState::STARTING;
    ServiceState aggregate = ServiceState::ONLINE;
    for (const auto& kv : latest_) {
        aggregate = combine(aggregate, kv.second.serviceState);
    }
    return aggregate;
}

}  // namespace aura
