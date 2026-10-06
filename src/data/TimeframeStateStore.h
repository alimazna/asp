#pragma once
// DAT-0014 - Per-timeframe state derived from the data bus.
//
// The store exposes the last closed bar and freshness per timeframe. It never
// promotes a candidate bar to closed. A timeframe with no closed bar reports
// UNKNOWN quality, not an empty-and-fine state.

#include "data/BarNormalizer.h"
#include "foundation/DataQualityState.h"
#include "foundation/Timestamp.h"
#include "mt5/Mt5BridgeContract.h"
#include "resilience/FreshnessState.h"

#include <map>
#include <mutex>
#include <string>

namespace aura {

struct TimeframeState {
    Timeframe timeframe = Timeframe::M15;
    bool hasClosedBar = false;
    Bar lastClosedBar;
    std::uint64_t sequence = 0;
    FreshnessInfo freshness;
    DataQualityState quality = DataQualityState::UNKNOWN;
};

class TimeframeStateStore {
public:
    TimeframeStateStore() = default;

    // Update from a finalized (already-closed) bar.
    void updateFromClosedBar(const Bar& closedBar, std::uint64_t sequence);

    // Mark the timeframe as not-yet-observed at `now`, computing freshness.
    void refreshFreshness(Timeframe timeframe, Timestamp now,
                          std::int64_t allowedIntervals = 2);

    bool get(Timeframe timeframe, TimeframeState& out) const;
    bool hasClosedBar(Timeframe timeframe) const;

    DataQualityState qualityOf(Timeframe timeframe) const;
    std::map<std::string, TimeframeState> snapshot() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, TimeframeState> states_;
};

}  // namespace aura
