#pragma once
// FND-0001 - Frozen foundational contract: deterministic entity identity.
//
// EntityId is an opaque, immutable, comparable value. It carries no
// generation policy: producers must derive it deterministically.

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

namespace aura {

class EntityId {
public:
    EntityId() = default;
    explicit EntityId(std::string value) : value_(std::move(value)) {}

    const std::string& value() const noexcept { return value_; }
    bool empty() const noexcept { return value_.empty(); }

    bool operator==(const EntityId& other) const noexcept { return value_ == other.value_; }
    bool operator!=(const EntityId& other) const noexcept { return !(*this == other); }
    bool operator<(const EntityId& other) const noexcept { return value_ < other.value_; }

private:
    std::string value_;
};

struct EntityIdHash {
    std::size_t operator()(const EntityId& id) const noexcept {
        return std::hash<std::string>{}(id.value());
    }
};

}  // namespace aura
