#pragma once
// FND-0002 - Frozen foundational contract: UTC instant with explicit availability.
//
// A Timestamp is either "unknown" or a concrete epoch-millisecond value.
// Unknown is never treated as a valid/neutral instant.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace aura {

class Timestamp {
public:
    using Millis = std::int64_t;
    static constexpr Millis kUnknown = -1;

    Timestamp() = default;

    static Timestamp fromEpochMillis(Millis ms) noexcept {
        Timestamp t;
        t.ms_ = ms;
        return t;
    }

    static Timestamp unknown() noexcept { return Timestamp{}; }

    static Timestamp now() noexcept {
        using namespace std::chrono;
        const auto ms = duration_cast<milliseconds>(
                            system_clock::now().time_since_epoch())
                            .count();
        return fromEpochMillis(static_cast<Millis>(ms));
    }

    bool isKnown() const noexcept { return ms_ >= 0; }
    bool isUnknown() const noexcept { return !isKnown(); }
    Millis epochMillis() const noexcept { return ms_; }

    bool operator==(const Timestamp& o) const noexcept { return ms_ == o.ms_; }
    bool operator!=(const Timestamp& o) const noexcept { return ms_ != o.ms_; }
    bool operator<(const Timestamp& o) const noexcept { return ms_ < o.ms_; }
    bool operator<=(const Timestamp& o) const noexcept { return ms_ <= o.ms_; }
    bool operator>(const Timestamp& o) const noexcept { return ms_ > o.ms_; }
    bool operator>=(const Timestamp& o) const noexcept { return ms_ >= o.ms_; }

private:
    Millis ms_ = kUnknown;
};

struct TimestampHash {
    std::size_t operator()(const Timestamp& t) const noexcept {
        return std::hash<Timestamp::Millis>{}(t.epochMillis());
    }
};

}  // namespace aura
