#pragma once
// FND-0007 - Frozen foundational contract: data-quality state.
//
// UNKNOWN is not neutral. Only VALID is decision-grade. This predicate is
// deliberately conservative and does not encode a capability decision.

#include <string>

namespace aura {

enum class DataQualityState {
    VALID,
    DEGRADED,
    INVALID,
    UNKNOWN,
    STALE,
    MISSING,
    OUT_OF_ORDER,
    DUPLICATE,
    INCOMPLETE,
};

inline const char* toString(DataQualityState q) noexcept {
    switch (q) {
        case DataQualityState::VALID:       return "VALID";
        case DataQualityState::DEGRADED:    return "DEGRADED";
        case DataQualityState::INVALID:     return "INVALID";
        case DataQualityState::UNKNOWN:     return "UNKNOWN";
        case DataQualityState::STALE:       return "STALE";
        case DataQualityState::MISSING:     return "MISSING";
        case DataQualityState::OUT_OF_ORDER:return "OUT_OF_ORDER";
        case DataQualityState::DUPLICATE:   return "DUPLICATE";
        case DataQualityState::INCOMPLETE:  return "INCOMPLETE";
    }
    return "UNKNOWN";
}

inline bool parseDataQualityState(const std::string& text, DataQualityState& out) noexcept {
    if (text == "VALID")        { out = DataQualityState::VALID;        return true; }
    if (text == "DEGRADED")     { out = DataQualityState::DEGRADED;     return true; }
    if (text == "INVALID")      { out = DataQualityState::INVALID;      return true; }
    if (text == "UNKNOWN")      { out = DataQualityState::UNKNOWN;      return true; }
    if (text == "STALE")        { out = DataQualityState::STALE;        return true; }
    if (text == "MISSING")      { out = DataQualityState::MISSING;      return true; }
    if (text == "OUT_OF_ORDER") { out = DataQualityState::OUT_OF_ORDER; return true; }
    if (text == "DUPLICATE")    { out = DataQualityState::DUPLICATE;    return true; }
    if (text == "INCOMPLETE")   { out = DataQualityState::INCOMPLETE;   return true; }
    return false;
}

// Decision-grade data must be VALID. Nothing else qualifies.
inline bool isDecisionGrade(DataQualityState q) noexcept {
    return q == DataQualityState::VALID;
}

}  // namespace aura
