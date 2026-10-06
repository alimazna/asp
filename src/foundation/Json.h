#pragma once
// Minimal, dependency-free JSON value with parse and serialize.
//
// Scope: enough for AURA's wire contracts (envelopes, events, records). It is
// deliberately strict about structure so malformed input is reported rather
// than coerced. Numbers are kept as text to preserve integer precision.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace aura {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue() = default;
    explicit JsonValue(bool b) : type_(Type::Bool), bool_(b) {}
    explicit JsonValue(double n) : type_(Type::Number), number_(std::to_string(n)) {}
    explicit JsonValue(std::int64_t n) : type_(Type::Number), number_(std::to_string(n)) {}
    explicit JsonValue(int n) : type_(Type::Number), number_(std::to_string(n)) {}
    explicit JsonValue(const char* s) : type_(Type::String), string_(s) {}
    explicit JsonValue(std::string s) : type_(Type::String), string_(std::move(s)) {}

    static JsonValue array() { JsonValue v; v.type_ = Type::Array; return v; }
    static JsonValue object() { JsonValue v; v.type_ = Type::Object; return v; }

    Type type() const noexcept { return type_; }
    bool isNull() const noexcept { return type_ == Type::Null; }
    bool isBool() const noexcept { return type_ == Type::Bool; }
    bool isNumber() const noexcept { return type_ == Type::Number; }
    bool isString() const noexcept { return type_ == Type::String; }
    bool isArray() const noexcept { return type_ == Type::Array; }
    bool isObject() const noexcept { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const noexcept {
        return type_ == Type::Bool ? bool_ : fallback;
    }
    double asDouble(double fallback = 0.0) const noexcept;
    std::int64_t asInt64(std::int64_t fallback = 0) const noexcept;
    const std::string& asString() const noexcept { return string_; }

    // Array access.
    const std::vector<JsonValue>& items() const noexcept { return array_; }
    std::size_t size() const noexcept;
    const JsonValue& at(std::size_t index) const;

    void push(JsonValue v);

    // Object access.
    bool has(const std::string& key) const;
    const JsonValue& operator[](const std::string& key) const;
    const std::map<std::string, JsonValue>& fields() const noexcept { return object_; }
    void set(const std::string& key, JsonValue v);

    std::string dump() const;

    // Returns false and sets `error` on malformed input.
    static bool parse(const std::string& text, JsonValue& out, std::string& error);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    std::string number_;
    std::string string_;
    std::vector<JsonValue> array_;
    std::map<std::string, JsonValue> object_;
};

}  // namespace aura
