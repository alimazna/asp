// RES-0019 - Resilience runtime: freshness monitor implementation.

#include "resilience/DataFreshnessMonitor.h"

namespace aura {

void DataFreshnessMonitor::recordUpdate(const std::string& stream, Timestamp at) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastUpdate_[stream] = at;
}

void DataFreshnessMonitor::setMaxAge(const std::string& stream,
                                     std::int64_t maxAgeMillis) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxAge_[stream] = maxAgeMillis;
}

bool DataFreshnessMonitor::hasStream(const std::string& stream) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastUpdate_.count(stream) != 0;
}

Timestamp DataFreshnessMonitor::lastUpdate(const std::string& stream) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = lastUpdate_.find(stream);
    return it == lastUpdate_.end() ? Timestamp::unknown() : it->second;
}

FreshnessInfo DataFreshnessMonitor::evaluate(const std::string& stream,
                                             Timestamp now) const {
    std::lock_guard<std::mutex> lock(mutex_);
    FreshnessInfo info;
    auto it = lastUpdate_.find(stream);
    if (it == lastUpdate_.end()) {
        info.state = FreshnessState::UNKNOWN;
        return info;
    }
    info.lastUpdate = it->second;
    auto ageIt = maxAge_.find(stream);
    info.maxAgeMillis = ageIt == maxAge_.end() ? -1 : ageIt->second;

    if (now.isUnknown() || it->second.isUnknown() || info.maxAgeMillis < 0) {
        info.state = FreshnessState::UNKNOWN;
        return info;
    }
    info.ageMillis = now.epochMillis() - it->second.epochMillis();
    if (info.ageMillis < 0) {
        // A last update in the future is not trustworthy.
        info.state = FreshnessState::UNKNOWN;
        return info;
    }
    info.state = info.ageMillis <= info.maxAgeMillis ? FreshnessState::FRESH
                                                     : FreshnessState::STALE;
    return info;
}

}  // namespace aura
