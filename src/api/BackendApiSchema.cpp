// PKG-0003 - Backend/frontend API schema implementation.

#include "api/BackendApiSchema.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>

namespace aura {

std::string jsonString(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                                  static_cast<unsigned int>(
                                      static_cast<unsigned char>(c)));
                    out += buffer;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

std::string jsonNumber(double value) {
    if (std::isnan(value) || std::isinf(value)) {
        // Never emit NaN/Infinity: they are not valid JSON and would imply a
        // value the backend cannot actually provide.
        return "null";
    }
    std::ostringstream out;
    out.precision(10);
    out << value;
    return out.str();
}

std::string jsonInteger(std::int64_t value) { return std::to_string(value); }

std::string jsonBool(bool value) { return value ? "true" : "false"; }

std::string isoUtcSeconds(std::int64_t seconds) {
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
        return std::string();
    }
    return std::string(buffer);
}

std::string jsonObject(const std::vector<ApiField>& fields) {
    std::ostringstream out;
    out << "{";
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) out << ",";
        out << jsonString(fields[i].name) << ":"
            << (fields[i].raw ? fields[i].value : jsonString(fields[i].value));
    }
    out << "}";
    return out.str();
}

std::string jsonArray(const std::vector<std::string>& encodedElements) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < encodedElements.size(); ++i) {
        if (i > 0) out << ",";
        out << encodedElements[i];
    }
    out << "]";
    return out.str();
}

ApiResponse errorResponse(int status, const std::string& code,
                          const std::string& message) {
    ApiResponse response;
    response.status = status;
    response.ok = false;
    response.contentType = "application/json";
    response.body = jsonObject({
        {"error", "true", false},  // contract: JSON string "true", not a boolean
        {"code", code},
        {"message", message},
    });
    return response;
}

std::string envelope(const std::string& dataJson) {
    std::ostringstream out;
    out << "{" << jsonString("api") << ":" << jsonString(kApiVersion) << ","
        << jsonString("schema") << ":" << jsonString(kApiSchemaVersion.toString())
        << "," << jsonString("data") << ":" << dataJson << "}";
    return out.str();
}

}  // namespace aura
