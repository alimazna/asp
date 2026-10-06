#pragma once
// RES-0018 - Resilience runtime: per-stream freshness tracking.
//
// A stream with no recorded update is UNKNOWN, never FRESH.

#include "foundation/Timestamp.h"
#include "resilience/FreshnessState.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace aura {

class DataFreshnessMonitor {
public:
    DataFreshnessMonitor() = default;

    void recordUpdate(const std::string& stream, Timestamp at);
    void setMaxAge(const std::string& stream, std::int64_t maxAgeMillis);

    bool hasStream(const std::string& stream) const;
    Timestamp lastUpdate(const std::string& stream) const;

    FreshnessInfo evaluate(const std::string& stream, Timestamp now) const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, Timestamp> lastUpdate_;
    std::map<std::string, std::int64_t> maxAge_;
};

}  // namespace aura
