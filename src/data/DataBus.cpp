// DAT-0013 - Data bus implementation.

#include "data/DataBus.h"

#include <utility>

namespace aura {

std::string DataBus::streamKey(Timeframe timeframe) {
    return toString(timeframe);
}

std::uint64_t DataBus::publish(const Bar& bar, const std::string& producer,
                               Timestamp eventTime) {
    BarEvent event;
    event.bar = bar;
    event.metadata.type = EventType::DATA_INGESTED;
    event.metadata.eventTime = eventTime;
    event.metadata.receiveTime = Timestamp::now();
    event.metadata.producer = EntityId(producer);
    event.metadata.source = bar.sourceBroker.empty() ? producer : bar.sourceBroker;

    std::vector<BarSubscriber> toNotify;
    std::uint64_t sequence = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string key = streamKey(bar.timeframe);
        sequence = ++sequences_[key];
        event.metadata.sequence = sequence;
        auto& stream = streams_[key];
        stream.push_back(bar);
        while (stream.size() > capacity_) stream.pop_front();
        toNotify.reserve(subscribers_.size());
        for (const auto& kv : subscribers_) toNotify.push_back(kv.second);
    }
    for (const auto& subscriber : toNotify) subscriber(event);
    return sequence;
}

std::uint64_t DataBus::subscribe(BarSubscriber subscriber) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t id = nextSubscriptionId_++;
    subscribers_[id] = std::move(subscriber);
    return id;
}

bool DataBus::unsubscribe(std::uint64_t subscriptionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscribers_.erase(subscriptionId) != 0;
}

std::vector<Bar> DataBus::recent(Timeframe timeframe, std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = streams_.find(streamKey(timeframe));
    if (it == streams_.end()) return {};
    const auto& stream = it->second;
    const std::size_t take = std::min(limit, stream.size());
    return std::vector<Bar>(stream.end() - static_cast<std::ptrdiff_t>(take), stream.end());
}

std::size_t DataBus::size(Timeframe timeframe) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = streams_.find(streamKey(timeframe));
    return it == streams_.end() ? 0 : it->second.size();
}

std::uint64_t DataBus::sequence(Timeframe timeframe) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sequences_.find(streamKey(timeframe));
    return it == sequences_.end() ? 0 : it->second;
}

}  // namespace aura
