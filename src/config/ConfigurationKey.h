#pragma once
// CFG-0001 - Configuration contract: a stable dotted configuration key.
// Keys are identity, not policy; they carry no default value.

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

namespace aura {

class ConfigurationKey {
public:
    ConfigurationKey() = default;
    explicit ConfigurationKey(std::string dotted) : dotted_(std::move(dotted)) {}

    const std::string& str() const noexcept { return dotted_; }
    bool empty() const noexcept { return dotted_.empty(); }

    bool operator==(const ConfigurationKey& o) const noexcept { return dotted_ == o.dotted_; }
    bool operator!=(const ConfigurationKey& o) const noexcept { return !(*this == o); }
    bool operator<(const ConfigurationKey& o) const noexcept { return dotted_ < o.dotted_; }

private:
    std::string dotted_;
};

struct ConfigurationKeyHash {
    std::size_t operator()(const ConfigurationKey& k) const noexcept {
        return std::hash<std::string>{}(k.str());
    }
};

}  // namespace aura
