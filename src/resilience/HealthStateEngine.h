#pragma once
// RES-0016 - Resilience runtime: derives aggregate health from observations.

#include "foundation/EntityId.h"
#include "foundation/ServiceState.h"
#include "resilience/HealthSnapshot.h"

#include <map>
#include <mutex>
#include <vector>

namespace aura {

class HealthStateEngine {
public:
    HealthStateEngine() = default;

    void observe(const HealthSnapshot& snapshot);

    bool latest(const EntityId& serviceId, HealthSnapshot& out) const;
    std::vector<HealthSnapshot> snapshots() const;

    // Most-severe state across all observed services.
    ServiceState aggregateState() const;

    // The mutex guarding `latest_`. Exposed so a caller can capture one
    // consistent view of the aggregate and the per-service detail under a
    // single lock, instead of taking several snapshots that could each see a
    // different tick. Callers must not call the public read methods while
    // holding it (they re-lock, which deadlocks).
    const std::mutex& mutex() const noexcept { return mutex_; }
    std::mutex& mutex() noexcept { return mutex_; }

    // Unlocked read helpers for callers already holding mutex(). Same result
    // as aggregateState()/snapshots() but without re-locking.
    ServiceState aggregateStateLocked() const noexcept {
        if (latest_.empty()) return ServiceState::STARTING;
        ServiceState aggregate = ServiceState::ONLINE;
        for (const auto& kv : latest_) {
            aggregate = combine(aggregate, kv.second.serviceState);
        }
        return aggregate;
    }

    std::vector<HealthSnapshot> snapshotsLocked() const {
        std::vector<HealthSnapshot> out;
        out.reserve(latest_.size());
        for (const auto& kv : latest_) out.push_back(kv.second);
        return out;
    }

    // Severity ordering helper (higher = worse).
    static int severityRank(ServiceState state) noexcept;
    static ServiceState combine(ServiceState a, ServiceState b) noexcept;

private:
    mutable std::mutex mutex_;
    std::map<EntityId, HealthSnapshot> latest_;
};

}  // namespace aura
