#pragma once
// RES-0005 - Resilience contract: a point-in-time health observation.

#include "foundation/DataQualityState.h"
#include "foundation/EntityId.h"
#include "foundation/ServiceState.h"
#include "foundation/Timestamp.h"

#include <string>

namespace aura {

struct HealthSnapshot {
    EntityId serviceId;
    std::string serviceName;
    ServiceState serviceState = ServiceState::STARTING;
    DataQualityState dataQuality = DataQualityState::UNKNOWN;
    Timestamp observedAt;
    std::string detail;

    bool isHealthy() const noexcept { return serviceState == ServiceState::ONLINE; }
    bool isUsable() const noexcept {
        return serviceState == ServiceState::ONLINE ||
               serviceState == ServiceState::DEGRADED;
    }
};

}  // namespace aura
