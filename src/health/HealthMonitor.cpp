// SHD-0016 - Health monitor implementation.

#include "health/HealthMonitor.h"

namespace aura {

void HealthMonitor::observeService(const EntityId& serviceId,
                                   const std::string& name, ServiceState state,
                                   DataQualityState dataQuality,
                                   const std::string& detail, Timestamp now) {
    HealthSnapshot snapshot;
    snapshot.serviceId = serviceId;
    snapshot.serviceName = name;
    snapshot.serviceState = state;
    snapshot.dataQuality = dataQuality;
    snapshot.detail = detail;
    snapshot.observedAt = now;
    engine_.observe(snapshot);
}

void HealthMonitor::observeBridge(IPythonBridgeClient& client, Timestamp now) {
    const auto health = client.health();
    if (!health.ok) {
        observeService(EntityId("mt5-python-bridge"), "mt5-python-bridge",
                       ServiceState::OFFLINE, DataQualityState::UNKNOWN,
                       health.error.message, now);
        return;
    }

    ServiceState state = ServiceState::ONLINE;
    if (!health.value.packageAvailable) {
        state = ServiceState::DEGRADED;
    }
    // MT5 not being ready is a degraded (not fatal) condition.
    if (!health.value.mt5Ready && state == ServiceState::ONLINE) {
        state = ServiceState::DEGRADED;
    }
    const DataQualityState quality = health.value.mt5Ready
                                         ? DataQualityState::VALID
                                         : DataQualityState::UNKNOWN;
    const std::string detail = health.value.mt5Ready
                                   ? "bridge and MT5 ready"
                                   : "bridge up, MT5 not ready: " + health.value.lastError;
    observeService(EntityId("mt5-python-bridge"), "mt5-python-bridge", state,
                   quality, detail, now);
}

void HealthMonitor::observeTimeframes(const TimeframeStateStore& store,
                                      Timestamp now) {
    for (Timeframe timeframe : allTimeframes()) {
        TimeframeState state;
        const std::string name = std::string("timeframe-") + toString(timeframe);
        if (!store.get(timeframe, state)) {
            observeService(EntityId(name), name, ServiceState::STARTING,
                           DataQualityState::UNKNOWN, "no state observed", now);
            continue;
        }
        ServiceState serviceState = ServiceState::ONLINE;
        if (!state.hasClosedBar) {
            serviceState = ServiceState::DEGRADED;
        } else if (!isDecisionGrade(state.quality)) {
            serviceState = ServiceState::DEGRADED;
        }
        observeService(EntityId(name), name, serviceState, state.quality,
                       state.hasClosedBar ? "closed bar present" : "no closed bar",
                       now);
    }
}

BackendHealth HealthMonitor::snapshot() const {
    BackendHealth health;
    health.observedAt = Timestamp::now();
    health.aggregate = engine_.aggregateState();

    for (const auto& snapshot : engine_.snapshots()) {
        health.services[snapshot.serviceName] = toString(snapshot.serviceState);
        if (snapshot.serviceState == ServiceState::DEGRADED ||
            !snapshot.isUsable()) {
            health.degradedReasons.push_back(snapshot.serviceName + ": " +
                                             snapshot.detail);
        }
        if (snapshot.serviceName.rfind("timeframe-", 0) == 0) {
            health.timeframes[snapshot.serviceName.substr(10)] =
                toString(snapshot.dataQuality);
        }
    }

    const DataQualityState m15 = [&]() -> DataQualityState {
        for (const auto& snapshot : engine_.snapshots()) {
            if (snapshot.serviceName == "timeframe-M15") return snapshot.dataQuality;
        }
        return DataQualityState::UNKNOWN;
    }();
    health.decisionGradeData = isDecisionGrade(m15);
    return health;
}

}  // namespace aura
