#pragma once
// FND-0015 - Frozen foundational contract: structured error codes.
// Codes are stable identifiers; they are never reused for new meanings.

#include <string>

namespace aura {

enum class ErrorCode {
    NONE,
    UNKNOWN_ERROR,
    CONFIG_INVALID,
    CONFIG_MISSING,
    DEPENDENCY_MISSING,
    BRIDGE_UNAVAILABLE,
    BRIDGE_PROTOCOL_MISMATCH,
    BRIDGE_SCHEMA_MISMATCH,
    MT5_TERMINAL_UNAVAILABLE,
    MT5_SYMBOL_UNRESOLVED,
    MARKET_DATA_MISSING,
    MARKET_DATA_INVALID,
    MARKET_DATA_STALE,
    MARKET_DATA_OUT_OF_ORDER,
    MARKET_DATA_DUPLICATE,
    MARKET_DATA_INCOMPLETE,
    PERSISTENCE_FAILURE,
    INTEGRITY_FAILURE,
    TIMEOUT,
    RESOURCE_EXHAUSTED,
    POLICY_VIOLATION,
    PERMISSION_DENIED,
    NOT_IMPLEMENTED,
    INTERNAL_ERROR,
};

inline const char* toString(ErrorCode c) noexcept {
    switch (c) {
        case ErrorCode::NONE:                     return "NONE";
        case ErrorCode::UNKNOWN_ERROR:            return "UNKNOWN_ERROR";
        case ErrorCode::CONFIG_INVALID:           return "CONFIG_INVALID";
        case ErrorCode::CONFIG_MISSING:           return "CONFIG_MISSING";
        case ErrorCode::DEPENDENCY_MISSING:       return "DEPENDENCY_MISSING";
        case ErrorCode::BRIDGE_UNAVAILABLE:       return "BRIDGE_UNAVAILABLE";
        case ErrorCode::BRIDGE_PROTOCOL_MISMATCH: return "BRIDGE_PROTOCOL_MISMATCH";
        case ErrorCode::BRIDGE_SCHEMA_MISMATCH:   return "BRIDGE_SCHEMA_MISMATCH";
        case ErrorCode::MT5_TERMINAL_UNAVAILABLE: return "MT5_TERMINAL_UNAVAILABLE";
        case ErrorCode::MT5_SYMBOL_UNRESOLVED:    return "MT5_SYMBOL_UNRESOLVED";
        case ErrorCode::MARKET_DATA_MISSING:      return "MARKET_DATA_MISSING";
        case ErrorCode::MARKET_DATA_INVALID:      return "MARKET_DATA_INVALID";
        case ErrorCode::MARKET_DATA_STALE:        return "MARKET_DATA_STALE";
        case ErrorCode::MARKET_DATA_OUT_OF_ORDER: return "MARKET_DATA_OUT_OF_ORDER";
        case ErrorCode::MARKET_DATA_DUPLICATE:    return "MARKET_DATA_DUPLICATE";
        case ErrorCode::MARKET_DATA_INCOMPLETE:   return "MARKET_DATA_INCOMPLETE";
        case ErrorCode::PERSISTENCE_FAILURE:      return "PERSISTENCE_FAILURE";
        case ErrorCode::INTEGRITY_FAILURE:        return "INTEGRITY_FAILURE";
        case ErrorCode::TIMEOUT:                  return "TIMEOUT";
        case ErrorCode::RESOURCE_EXHAUSTED:       return "RESOURCE_EXHAUSTED";
        case ErrorCode::POLICY_VIOLATION:         return "POLICY_VIOLATION";
        case ErrorCode::PERMISSION_DENIED:        return "PERMISSION_DENIED";
        case ErrorCode::NOT_IMPLEMENTED:          return "NOT_IMPLEMENTED";
        case ErrorCode::INTERNAL_ERROR:           return "INTERNAL_ERROR";
    }
    return "UNKNOWN_ERROR";
}

inline bool parseErrorCode(const std::string& text, ErrorCode& out) noexcept {
    static const ErrorCode all[] = {
        ErrorCode::NONE, ErrorCode::UNKNOWN_ERROR, ErrorCode::CONFIG_INVALID,
        ErrorCode::CONFIG_MISSING, ErrorCode::DEPENDENCY_MISSING,
        ErrorCode::BRIDGE_UNAVAILABLE, ErrorCode::BRIDGE_PROTOCOL_MISMATCH,
        ErrorCode::BRIDGE_SCHEMA_MISMATCH, ErrorCode::MT5_TERMINAL_UNAVAILABLE,
        ErrorCode::MT5_SYMBOL_UNRESOLVED, ErrorCode::MARKET_DATA_MISSING,
        ErrorCode::MARKET_DATA_INVALID, ErrorCode::MARKET_DATA_STALE,
        ErrorCode::MARKET_DATA_OUT_OF_ORDER, ErrorCode::MARKET_DATA_DUPLICATE,
        ErrorCode::MARKET_DATA_INCOMPLETE, ErrorCode::PERSISTENCE_FAILURE,
        ErrorCode::INTEGRITY_FAILURE, ErrorCode::TIMEOUT,
        ErrorCode::RESOURCE_EXHAUSTED, ErrorCode::POLICY_VIOLATION,
        ErrorCode::PERMISSION_DENIED, ErrorCode::NOT_IMPLEMENTED,
        ErrorCode::INTERNAL_ERROR};
    for (ErrorCode c : all) {
        if (text == toString(c)) { out = c; return true; }
    }
    return false;
}

}  // namespace aura
