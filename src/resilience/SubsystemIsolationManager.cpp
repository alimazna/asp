// RES-0023 - Resilience runtime: subsystem isolation implementation.

#include "resilience/SubsystemIsolationManager.h"

namespace aura {

void SubsystemIsolationManager::isolate(const CapabilityId& id,
                                        const std::string& reason) {
    if (id.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    isolated_[id] = reason;
}

void SubsystemIsolationManager::release(const CapabilityId& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    isolated_.erase(id);
}

bool SubsystemIsolationManager::isIsolated(const CapabilityId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isolated_.count(id) != 0;
}

std::string SubsystemIsolationManager::reasonFor(const CapabilityId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = isolated_.find(id);
    return it == isolated_.end() ? std::string() : it->second;
}

std::vector<CapabilityId> SubsystemIsolationManager::isolated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CapabilityId> out;
    out.reserve(isolated_.size());
    for (const auto& kv : isolated_) out.push_back(kv.first);
    return out;
}

}  // namespace aura
