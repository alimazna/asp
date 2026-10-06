#pragma once
// DAT-0012 - In-memory fan-out bus for validated bar events.
//
// The bus is append-only per stream and preserves per-stream sequence. It
// carries no decision authority: subscribers observe, they do not mutate.

#include "data/BarNormalizer.h"
#include "foundation/EventMetadata.h"
#include "foundation/Timestamp.h"
#include "mt5/Mt5BridgeContract.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace aura {

struct BarEvent {
    EventMetadata metadata;
    Bar bar;
};

using BarSubscriber = std::function<void(const BarEvent&)>;

class DataBus {
public:
    explicit DataBus(std::size_t perStreamCapacity = 4096)
        : capacity_(perStreamCapacity) {}

    // Publish a validated bar. Returns the assigned per-stream sequence.
    std::uint64_t publish(const Bar& bar, const std::string& producer,
                          Timestamp eventTime);

    std::uint64_t subscribe(BarSubscriber subscriber);
    bool unsubscribe(std::uint64_t subscriptionId);

    // Most recent bars for a timeframe, ascending, up to `limit`.
    std::vector<Bar> recent(Timeframe timeframe, std::size_t limit) const;

    std::size_t size(Timeframe timeframe) const;
    std::uint64_t sequence(Timeframe timeframe) const;

private:
    static std::string streamKey(Timeframe timeframe);

    mutable std::mutex mutex_;
    std::size_t capacity_;
    std::map<std::string, std::deque<Bar>> streams_;
    std::map<std::string, std::uint64_t> sequences_;
    std::map<std::uint64_t, BarSubscriber> subscribers_;
    std::uint64_t nextSubscriptionId_ = 1;
};

}  // namespace aura
