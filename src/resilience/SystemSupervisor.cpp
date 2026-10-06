// RES-0011 - Resilience runtime: system supervisor implementation.

#include "resilience/SystemSupervisor.h"

namespace aura {

void SystemSupervisor::registerService(const ServiceDescriptor& descriptor) {
    std::lock_guard<std::mutex> lock(mutex_);
    services_[descriptor.id] = descriptor;
    if (states_.count(descriptor.id) == 0) {
        states_[descriptor.id] = ServiceState::STARTING;
    }
}

bool SystemSupervisor::setServiceState(const EntityId& serviceId,
                                       ServiceState state,
                                       const std::string& detail) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (services_.count(serviceId) == 0) return false;
    states_[serviceId] = state;

    HealthSnapshot snap;
    snap.serviceId = serviceId;
    snap.serviceName = services_[serviceId].name;
    snap.serviceState = state;
    snap.observedAt = Timestamp::now();
    snap.detail = detail;
    health_.observe(snap);
    return true;
}

bool SystemSupervisor::serviceState(const EntityId& serviceId,
                                    ServiceState& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(serviceId);
    if (it == states_.end()) return false;
    out = it->second;
    return true;
}

std::vector<ServiceDescriptor> SystemSupervisor::services() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ServiceDescriptor> out;
    out.reserve(services_.size());
    for (const auto& kv : services_) out.push_back(kv.second);
    return out;
}

SystemMode SystemSupervisor::deriveSystemMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (services_.empty()) return SystemMode::STARTING;

    bool anyStarting = false;
    bool anyError = false;
    bool anyBlocked = false;
    bool anyOffline = false;
    bool anyDegraded = false;
    bool anyRecovering = false;
    bool allOnline = true;

    for (const auto& kv : states_) {
        switch (kv.second) {
            case ServiceState::STARTING:   anyStarting = true; allOnline = false; break;
            case ServiceState::ONLINE:     break;
            case ServiceState::DEGRADED:   anyDegraded = true; allOnline = false; break;
            case ServiceState::RECOVERING: anyRecovering = true; allOnline = false; break;
            case ServiceState::PAUSED:     allOnline = false; break;
            case ServiceState::BLOCKED:    anyBlocked = true; allOnline = false; break;
            case ServiceState::OFFLINE:    anyOffline = true; allOnline = false; break;
            case ServiceState::ERROR:      anyError = true; allOnline = false; break;
        }
    }

    if (anyError) return SystemMode::EMERGENCY;
    if (anyBlocked) return SystemMode::DEGRADED;
    if (anyOffline) return SystemMode::DEGRADED;
    if (anyStarting) return SystemMode::STARTING;
    if (anyRecovering) return SystemMode::RECOVERY;
    if (anyDegraded) return SystemMode::DEGRADED;
    if (allOnline) return SystemMode::SHADOW;
    return SystemMode::DEGRADED;
}

}  // namespace aura
