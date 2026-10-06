#pragma once
// RES-0002 - Resilience contract: a capability identifier.
// Capabilities are the units of enablement/disablement under degradation.

#include "foundation/EntityId.h"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

namespace aura {

class CapabilityId {
public:
    CapabilityId() = default;
    explicit CapabilityId(std::string value) : value_(std::move(value)) {}
    explicit CapabilityId(EntityId id) : value_(id.value()) {}

    const std::string& value() const noexcept { return value_; }
    bool empty() const noexcept { return value_.empty(); }

    bool operator==(const CapabilityId& o) const noexcept { return value_ == o.value_; }
    bool operator!=(const CapabilityId& o) const noexcept { return !(*this == o); }
    bool operator<(const CapabilityId& o) const noexcept { return value_ < o.value_; }

private:
    std::string value_;
};

struct CapabilityIdHash {
    std::size_t operator()(const CapabilityId& id) const noexcept {
        return std::hash<std::string>{}(id.value());
    }
};

}  // namespace aura
