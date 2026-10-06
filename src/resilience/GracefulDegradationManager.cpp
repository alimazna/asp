// RES-0025 - Resilience runtime: graceful degradation implementation.

#include "resilience/GracefulDegradationManager.h"

namespace aura {

void GracefulDegradationManager::applyImpacts(
    const std::vector<DegradationImpact>& impacts) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& impact : impacts) {
        if (impact.capability.empty()) continue;
        auto it = active_.find(impact.capability);
        if (it == active_.end() ||
            static_cast<int>(impact.level) > static_cast<int>(it->second.level)) {
            active_[impact.capability] = impact;
        }
    }
}

void GracefulDegradationManager::clearCause(const CapabilityId& cause) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = active_.begin(); it != active_.end();) {
        if (it->second.cause == cause || it->first == cause) {
            it = active_.erase(it);
        } else {
            ++it;
        }
    }
}

bool GracefulDegradationManager::isEnabled(const CapabilityId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_.find(id);
    if (it == active_.end()) return true;
    return it->second.level != ImpactLevel::BLOCKED;
}

bool GracefulDegradationManager::isDegraded(const CapabilityId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_.find(id);
    return it != active_.end() && it->second.level == ImpactLevel::DEGRADED;
}

std::map<CapabilityId, ImpactLevel> GracefulDegradationManager::activeImpacts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<CapabilityId, ImpactLevel> out;
    for (const auto& kv : active_) out[kv.first] = kv.second.level;
    return out;
}

}  // namespace aura
