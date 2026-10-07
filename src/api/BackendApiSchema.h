#pragma once
// PKG-0003 - Backend/frontend API schema.
//
// Defines the versioned wire contract shared by the backend facade and the
// Alpha frontend. Serialization is deterministic and dependency-free; the
// schema never emits a value it cannot source.

#include "foundation/SchemaVersion.h"

#include <cstdint>
#include <string>
#include <vector>

namespace aura {

inline constexpr const char* kApiVersion = "v1";
inline constexpr SchemaVersion kApiSchemaVersion{1, 0};

struct ApiResponse {
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
    bool ok = true;
};

struct ApiField {
    std::string name;
    std::string value;   // already JSON-encoded (quoted string, number, etc.)
    bool raw = false;    // when true, `value` is emitted verbatim
};

// Escape a string into a JSON string literal (including the surrounding
// quotes). Control characters are escaped; invalid UTF-8 is not repaired.
std::string jsonString(const std::string& value);

// Format helpers. Numbers are formatted deterministically.
std::string jsonNumber(double value);
std::string jsonInteger(std::int64_t value);
std::string jsonBool(bool value);

// Format an epoch-second instant as an ISO-8601 UTC string ("...Z"). Returns an
// empty string when the instant cannot be formatted (the caller emits null).
std::string isoUtcSeconds(std::int64_t seconds);

// Assemble an object from fields, preserving field order.
std::string jsonObject(const std::vector<ApiField>& fields);
std::string jsonArray(const std::vector<std::string>& encodedElements);

// Build a response whose body is an error object. Used for every non-2xx.
ApiResponse errorResponse(int status, const std::string& code,
                          const std::string& message);

// Wrap a payload in the standard envelope: {api, schema, data}.
std::string envelope(const std::string& dataJson);

}  // namespace aura
