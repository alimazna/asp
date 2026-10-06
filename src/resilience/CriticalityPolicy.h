#pragma once
// RES-0030 - Resilience runtime: capability criticality classification.
//
// Criticality decides whether a failure blocks the whole system or only the
// dependent capabilities. It is policy, not an implicit decision.

#include "resilience/CapabilityId.h"

#include <map>
#include <mutex>

namespace aura {

enum class Criticality {
    CRITICAL,   // failure degrades the whole system
    IMPORTANT,  // failure blocks dependents but system continues
    OPTIONAL,   // failure is isolated and non-blocking
};

inline const char* toString(Criticality c) noexcept {
    switch (c) {
        case Criticality::CRITICAL:  return "CRITICAL";
        case Criticality::IMPORTANT: return "IMPORTANT";
        case Criticality::OPTIONAL:  return "OPTIONAL";
    }
    return "IMPORTANT";
}

class CriticalityPolicy {
public:
    void set(const CapabilityId& id, Criticality criticality);
    Criticality of(const CapabilityId& id) const;   // defaults to IMPORTANT

private:
    mutable std::mutex mutex_;
    std::map<CapabilityId, Criticality> map_;
};

}  // namespace aura
