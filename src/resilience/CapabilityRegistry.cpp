// RES-0013 - Resilience runtime: capability registry implementation.

#include "resilience/CapabilityRegistry.h"

namespace aura {

bool CapabilityRegistry::registerCapability(const CapabilityDescriptor& descriptor) {
    if (descriptor.id.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = capabilities_.emplace(descriptor.id, descriptor);
    return result.second;
}

bool CapabilityRegistry::updateState(const CapabilityId& id, ServiceState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = capabilities_.find(id);
    if (it == capabilities_.end()) return false;
    it->second.initialState = state;
    return true;
}

bool CapabilityRegistry::find(const CapabilityId& id,
                              CapabilityDescriptor& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = capabilities_.find(id);
    if (it == capabilities_.end()) return false;
    out = it->second;
    return true;
}

bool CapabilityRegistry::contains(const CapabilityId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capabilities_.count(id) != 0;
}

ServiceState CapabilityRegistry::stateOf(const CapabilityId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = capabilities_.find(id);
    return it == capabilities_.end() ? ServiceState::OFFLINE
                                     : it->second.initialState;
}

std::vector<CapabilityDescriptor> CapabilityRegistry::all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CapabilityDescriptor> out;
    out.reserve(capabilities_.size());
    for (const auto& kv : capabilities_) out.push_back(kv.second);
    return out;
}

std::vector<CapabilityId> CapabilityRegistry::ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CapabilityId> out;
    out.reserve(capabilities_.size());
    for (const auto& kv : capabilities_) out.push_back(kv.first);
    return out;
}

std::size_t CapabilityRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capabilities_.size();
}

}  // namespace aura
