#pragma once
// FND-0011 - Frozen foundational contract: event identity.
// An EventId is an EntityId scoped to the event/audit boundary.

#include "foundation/EntityId.h"

#include <string>
#include <utility>

namespace aura {

class EventId {
public:
    EventId() = default;
    explicit EventId(EntityId id) : id_(std::move(id)) {}
    explicit EventId(std::string value) : id_(EntityId(std::move(value))) {}

    const EntityId& entity() const noexcept { return id_; }
    const std::string& value() const noexcept { return id_.value(); }
    bool empty() const noexcept { return id_.empty(); }

    bool operator==(const EventId& o) const noexcept { return id_ == o.id_; }
    bool operator!=(const EventId& o) const noexcept { return !(*this == o); }
    bool operator<(const EventId& o) const noexcept { return id_ < o.id_; }

private:
    EntityId id_;
};

}  // namespace aura
