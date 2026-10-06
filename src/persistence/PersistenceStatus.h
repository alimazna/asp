#pragma once
// PER-0001 - Persistence contract: the result of a persistence operation.

#include <string>

namespace aura {

enum class PersistenceStatus {
    OK,
    NOT_FOUND,
    CONFLICT,
    IO_ERROR,
    CORRUPT,
    UNAVAILABLE,
    READ_ONLY,
    IDEMPOTENT_NO_OP,
};

inline const char* toString(PersistenceStatus s) noexcept {
    switch (s) {
        case PersistenceStatus::OK:               return "OK";
        case PersistenceStatus::NOT_FOUND:        return "NOT_FOUND";
        case PersistenceStatus::CONFLICT:         return "CONFLICT";
        case PersistenceStatus::IO_ERROR:         return "IO_ERROR";
        case PersistenceStatus::CORRUPT:          return "CORRUPT";
        case PersistenceStatus::UNAVAILABLE:      return "UNAVAILABLE";
        case PersistenceStatus::READ_ONLY:        return "READ_ONLY";
        case PersistenceStatus::IDEMPOTENT_NO_OP: return "IDEMPOTENT_NO_OP";
    }
    return "UNKNOWN";
}

inline bool isPersistenceSuccess(PersistenceStatus s) noexcept {
    return s == PersistenceStatus::OK || s == PersistenceStatus::IDEMPOTENT_NO_OP;
}

}  // namespace aura
