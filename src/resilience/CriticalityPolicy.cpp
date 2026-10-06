// RES-0031 - Resilience runtime: criticality policy implementation.

#include "resilience/CriticalityPolicy.h"

namespace aura {

void CriticalityPolicy::set(const CapabilityId& id, Criticality criticality) {
    if (id.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    map_[id] = criticality;
}

Criticality CriticalityPolicy::of(const CapabilityId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = map_.find(id);
    return it == map_.end() ? Criticality::IMPORTANT : it->second;
}

}  // namespace aura
