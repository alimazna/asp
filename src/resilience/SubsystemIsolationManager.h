#pragma once
// RES-0022 - Resilience runtime: isolates a failed subsystem so its failure
// does not cascade into unrelated capabilities.

#include "resilience/CapabilityId.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace aura {

class SubsystemIsolationManager {
public:
    SubsystemIsolationManager() = default;

    void isolate(const CapabilityId& id, const std::string& reason);
    void release(const CapabilityId& id);

    bool isIsolated(const CapabilityId& id) const;
    std::string reasonFor(const CapabilityId& id) const;
    std::vector<CapabilityId> isolated() const;

private:
    mutable std::mutex mutex_;
    std::map<CapabilityId, std::string> isolated_;
};

}  // namespace aura
