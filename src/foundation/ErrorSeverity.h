#pragma once
// FND-0014 - Frozen foundational contract: error severity.

#include <string>

namespace aura {

enum class ErrorSeverity {
    INFO,
    WARNING,
    ERROR,
    CRITICAL,
    FATAL,
};

inline const char* toString(ErrorSeverity s) noexcept {
    switch (s) {
        case ErrorSeverity::INFO:     return "INFO";
        case ErrorSeverity::WARNING:  return "WARNING";
        case ErrorSeverity::ERROR:    return "ERROR";
        case ErrorSeverity::CRITICAL: return "CRITICAL";
        case ErrorSeverity::FATAL:    return "FATAL";
    }
    return "UNKNOWN";
}

inline bool isBlocking(ErrorSeverity s) noexcept {
    return s == ErrorSeverity::CRITICAL || s == ErrorSeverity::FATAL;
}

}  // namespace aura
