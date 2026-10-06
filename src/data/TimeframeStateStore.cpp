// DAT-0015 - Timeframe state store implementation.

#include "data/TimeframeStateStore.h"

#include "resilience/StaleCandleDetector.h"

namespace aura {

void TimeframeStateStore::updateFromClosedBar(const Bar& closedBar,
                                              std::uint64_t sequence) {
    std::lock_guard<std::mutex> lock(mutex_);
    TimeframeState& state = states_[toString(closedBar.timeframe)];
    state.timeframe = closedBar.timeframe;
    state.hasClosedBar = true;
    state.lastClosedBar = closedBar;
    state.sequence = sequence;
    state.quality = closedBar.quality;
}

void TimeframeStateStore::refreshFreshness(Timeframe timeframe, Timestamp now,
                                           std::int64_t allowedIntervals) {
    std::lock_guard<std::mutex> lock(mutex_);
    TimeframeState& state = states_[toString(timeframe)];
    state.timeframe = timeframe;

    if (!state.hasClosedBar) {
        state.freshness.state = FreshnessState::UNKNOWN;
        state.freshness.lastUpdate = Timestamp::unknown();
        state.freshness.ageMillis = -1;
        return;
    }

    const Bar& bar = state.lastClosedBar;
    const Timestamp barOpen = Timestamp::fromEpochMillis(bar.openTimeSec * 1000);
    const FreshnessState freshness =
        StaleCandleDetector::classify(barOpen, now, intervalMillis(timeframe),
                                      static_cast<int>(allowedIntervals));
    state.freshness.state = freshness;
    state.freshness.lastUpdate = barOpen;
    state.freshness.maxAgeMillis = intervalMillis(timeframe) * (allowedIntervals + 1);
    if (now.isKnown()) {
        state.freshness.ageMillis = now.epochMillis() - barOpen.epochMillis();
    }
    if (freshness != FreshnessState::FRESH) {
        state.quality = freshness == FreshnessState::STALE
                            ? DataQualityState::STALE
                            : DataQualityState::UNKNOWN;
    }
}

bool TimeframeStateStore::get(Timeframe timeframe, TimeframeState& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(toString(timeframe));
    if (it == states_.end()) return false;
    out = it->second;
    return true;
}

bool TimeframeStateStore::hasClosedBar(Timeframe timeframe) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(toString(timeframe));
    return it != states_.end() && it->second.hasClosedBar;
}

DataQualityState TimeframeStateStore::qualityOf(Timeframe timeframe) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(toString(timeframe));
    return it == states_.end() ? DataQualityState::UNKNOWN : it->second.quality;
}

std::map<std::string, TimeframeState> TimeframeStateStore::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return states_;
}

}  // namespace aura
