#pragma once
// SHD-0015 - Backend health monitor.
//
// Aggregates subsystem health observations into a single, queryable view for
// the API layer. UNKNOWN is never reported as healthy.

#include "data/TimeframeStateStore.h"
#include "foundation/ServiceState.h"
#include "foundation/Timestamp.h"
#include "mt5/PythonBridgeClient.h"
#include "resilience/HealthStateEngine.h"
#include "resilience/HealthSnapshot.h"

#include <map>
#include <string>
#include <vector>

namespace aura {

struct BackendHealth {
    ServiceState aggregate = ServiceState::STARTING;
    bool decisionGradeData = false;
    std::vector<std::string> degradedReasons;
    std::map<std::string, std::string> services;
    std::map<std::string, std::string> timeframes;
    Timestamp observedAt;
};

class HealthMonitor {
public:
    HealthMonitor() = default;

    void observeService(const EntityId& serviceId, const std::string& name,
                        ServiceState state, DataQualityState dataQuality,
                        const std::string& detail, Timestamp now);

    // Observe bridge health through the client contract.
    void observeBridge(IPythonBridgeClient& client, Timestamp now);

    // Observe timeframe freshness from the state store.
    void observeTimeframes(const TimeframeStateStore& store, Timestamp now);

    BackendHealth snapshot() const;

    const HealthStateEngine& engine() const noexcept { return engine_; }

private:
    HealthStateEngine engine_;
};

}  // namespace aura
