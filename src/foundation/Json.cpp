// Minimal JSON implementation.

#include "foundation/Json.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace aura {

namespace {

const JsonValue& nullValue() {
    static const JsonValue kNull;
    return kNull;
}

void escapeInto(const std::string& s, std::string& out) {
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void dumpInto(const JsonValue& v, std::string& out);

void dumpObjectInto(const std::map<std::string, JsonValue>& fields, std::string& out) {
    out.push_back('{');
    bool first = true;
    for (const auto& kv : fields) {
        if (!first) out.push_back(',');
        first = false;
        escapeInto(kv.first, out);
        out.push_back(':');
        dumpInto(kv.second, out);
    }
    out.push_back('}');
}

void dumpInto(const JsonValue& v, std::string& out) {
    switch (v.type()) {
        case JsonValue::Type::Null:   out += "null"; break;
        case JsonValue::Type::Bool:   out += v.asBool() ? "true" : "false"; break;
        case JsonValue::Type::Number: out += v.asString(); break;
        case JsonValue::Type::String: escapeInto(v.asString(), out); break;
        case JsonValue::Type::Array: {
            out.push_back('[');
            bool first = true;
            for (const auto& item : v.items()) {
                if (!first) out.push_back(',');
                first = false;
                dumpInto(item, out);
            }
            out.push_back(']');
            break;
        }
        case JsonValue::Type::Object:
            dumpObjectInto(v.fields(), out);
            break;
    }
}

struct Parser {
    const std::string& text;
    std::size_t pos = 0;
    std::string error;

    explicit Parser(const std::string& t) : text(t) {}

    void skipWs() {
        while (pos < text.size() &&
               std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    }

    bool fail(const std::string& msg) {
        if (error.empty()) {
            error = msg + " at offset " + std::to_string(pos);
        }
        return false;
    }

    bool parseValue(JsonValue& out) {
        skipWs();
        if (pos >= text.size()) return fail("unexpected end of input");
        char c = text[pos];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') {
            std::string s;
            if (!parseString(s)) return false;
            out = JsonValue(s);
            return true;
        }
        if (c == 't' || c == 'f') return parseBool(out);
        if (c == 'n') return parseNull(out);
        return parseNumber(out);
    }

    bool parseObject(JsonValue& out) {
        out = JsonValue::object();
        ++pos;  // {
        skipWs();
        if (pos < text.size() && text[pos] == '}') { ++pos; return true; }
        while (true) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (pos >= text.size() || text[pos] != ':') return fail("expected ':'");
            ++pos;
            JsonValue value;
            if (!parseValue(value)) return false;
            out.set(key, std::move(value));
            skipWs();
            if (pos >= text.size()) return fail("unterminated object");
            if (text[pos] == ',') { ++pos; continue; }
            if (text[pos] == '}') { ++pos; return true; }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray(JsonValue& out) {
        out = JsonValue::array();
        ++pos;  // [
        skipWs();
        if (pos < text.size() && text[pos] == ']') { ++pos; return true; }
        while (true) {
            JsonValue value;
            if (!parseValue(value)) return false;
            out.push(std::move(value));
            skipWs();
            if (pos >= text.size()) return fail("unterminated array");
            if (text[pos] == ',') { ++pos; continue; }
            if (text[pos] == ']') { ++pos; return true; }
            return fail("expected ',' or ']'");
        }
    }

    bool parseString(std::string& out) {
        if (pos >= text.size() || text[pos] != '"') return fail("expected string");
        ++pos;
        while (pos < text.size()) {
            char c = text[pos++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos >= text.size()) return fail("bad escape");
                char e = text[pos++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': {
                        if (pos + 4 > text.size()) return fail("bad unicode escape");
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = text[pos++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= (h - '0');
                            else if (h >= 'a' && h <= 'f') code |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= (h - 'A' + 10);
                            else return fail("bad hex digit");
                        }
                        // Encode as UTF-8 (BMP only).
                        if (code < 0x80) {
                            out.push_back(static_cast<char>(code));
                        } else if (code < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        break;
                    }
                    default: return fail("unknown escape");
                }
            } else {
                out.push_back(c);
            }
        }
        return fail("unterminated string");
    }

    bool parseBool(JsonValue& out) {
        if (text.compare(pos, 4, "true") == 0) { pos += 4; out = JsonValue(true); return true; }
        if (text.compare(pos, 5, "false") == 0) { pos += 5; out = JsonValue(false); return true; }
        return fail("invalid literal");
    }

    bool parseNull(JsonValue& out) {
        if (text.compare(pos, 4, "null") == 0) { pos += 4; out = JsonValue(); return true; }
        return fail("invalid literal");
    }

    bool parseNumber(JsonValue& out) {
        std::size_t start = pos;
        if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) ++pos;
        bool anyDigit = false;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            ++pos;
            anyDigit = true;
        }
        if (pos < text.size() && text[pos] == '.') {
            ++pos;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                ++pos;
                anyDigit = true;
            }
        }
        if (!anyDigit) return fail("invalid number");
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
            ++pos;
            if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) ++pos;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
        }
        out = JsonValue(text.substr(start, pos - start));
        return true;
    }
};

}  // namespace

double JsonValue::asDouble(double fallback) const noexcept {
    if (type_ != Type::Number) return fallback;
    try {
        return std::stod(number_);
    } catch (...) {
        return fallback;
    }
}

std::int64_t JsonValue::asInt64(std::int64_t fallback) const noexcept {
    if (type_ != Type::Number) return fallback;
    try {
        return static_cast<std::int64_t>(std::stoll(number_));
    } catch (...) {
        try {
            return static_cast<std::int64_t>(std::stod(number_));
        } catch (...) {
            return fallback;
        }
    }
}

std::size_t JsonValue::size() const noexcept {
    if (type_ == Type::Array) return array_.size();
    if (type_ == Type::Object) return object_.size();
    return 0;
}

const JsonValue& JsonValue::at(std::size_t index) const {
    if (type_ != Type::Array || index >= array_.size()) return nullValue();
    return array_[index];
}

void JsonValue::push(JsonValue v) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        array_.clear();
    }
    array_.push_back(std::move(v));
}

bool JsonValue::has(const std::string& key) const {
    return type_ == Type::Object && object_.count(key) != 0;
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    if (type_ != Type::Object) return nullValue();
    auto it = object_.find(key);
    return it == object_.end() ? nullValue() : it->second;
}

void JsonValue::set(const std::string& key, JsonValue v) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
    }
    object_[key] = std::move(v);
}

std::string JsonValue::dump() const {
    std::string out;
    dumpInto(*this, out);
    return out;
}

bool JsonValue::parse(const std::string& text, JsonValue& out, std::string& error) {
    Parser parser(text);
    if (!parser.parseValue(out)) {
        error = parser.error;
        return false;
    }
    parser.skipWs();
    if (parser.pos != text.size()) {
        error = "trailing characters at offset " + std::to_string(parser.pos);
        return false;
    }
    error.clear();
    return true;
}

}  // namespace aura
