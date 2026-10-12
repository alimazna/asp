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

    BackendHealth snapshot();

    // Build a BackendHealth from the current observations while holding the
    // engine lock. Public so refreshCache() can capture one consistent view
    // under a single lock.
    BackendHealth buildSnapshotLocked() const;

    // Called by the runtime tick after it has observed every service. It
    // captures one consistent BackendHealth under the engine lock and publishes
    // it, so the API layer can answer /health by reading a cached copy without
    // touching the engine, the tracker, or the bridge at all. A /health request
    // therefore never blocks on the runtime tick — even when the tick is in a
    // multi-second bridge proxy — and never serializes behind it.
    void refreshCache(Timestamp now) {
        BackendHealth snap;
        {
            std::lock_guard<std::mutex> lock(engine_.mutex());
            snap = buildSnapshotLocked();
        }
        snap.observedAt = now;
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cached_ = std::move(snap);
    }

    // Read the snapshot most recently published by refreshCache(). The cache
    // mutex is held only for the copy, so this returns in microseconds even if
    // the runtime tick is mid-observation. Before the first tick the cache
    // holds a default snapshot (aggregate=STARTING), which is the honest state.
    BackendHealth readCache() const {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        return cached_;
    }

    const HealthStateEngine& engine() const noexcept { return engine_; }

private:
    HealthStateEngine engine_;
    mutable std::mutex cacheMutex_;
    BackendHealth cached_;   // aggregate=STARTING until the first tick publishes
};

}  // namespace aura
