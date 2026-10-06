#pragma once
// CFG-0002 - Configuration contract: a typed, immutable configuration value.
// Values are explicit about their type; there is no implicit coercion.

#include "config/ConfigurationKey.h"

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace aura {

class ConfigurationValue {
public:
    using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

    ConfigurationValue() = default;
    explicit ConfigurationValue(bool v) : value_(v) {}
    explicit ConfigurationValue(std::int64_t v) : value_(v) {}
    explicit ConfigurationValue(double v) : value_(v) {}
    explicit ConfigurationValue(std::string v) : value_(std::move(v)) {}

    bool isUnset() const noexcept { return value_.index() == 0; }
    bool isBool() const noexcept { return std::holds_alternative<bool>(value_); }
    bool isInteger() const noexcept { return std::holds_alternative<std::int64_t>(value_); }
    bool isNumber() const noexcept { return std::holds_alternative<double>(value_); }
    bool isString() const noexcept { return std::holds_alternative<std::string>(value_); }

    bool asBool(bool fallback = false) const noexcept {
        return isBool() ? std::get<bool>(value_) : fallback;
    }
    std::int64_t asInteger(std::int64_t fallback = 0) const noexcept {
        return isInteger() ? std::get<std::int64_t>(value_) : fallback;
    }
    double asNumber(double fallback = 0.0) const noexcept {
        return isNumber() ? std::get<double>(value_) : fallback;
    }
    std::string asString(const std::string& fallback = {}) const {
        return isString() ? std::get<std::string>(value_) : fallback;
    }

    const Storage& storage() const noexcept { return value_; }

private:
    Storage value_{};
};

}  // namespace aura
