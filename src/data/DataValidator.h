#pragma once
// DAT-0008 - Validates normalized bars and sequences.
//
// The validator classifies; it never repairs. Every defect is reported with a
// code so the failure can be audited and propagated. No fabricated values.

#include "data/BarNormalizer.h"
#include "foundation/DataQualityState.h"
#include "foundation/Timestamp.h"

#include <string>
#include <vector>

namespace aura {

enum class BarDefect {
    NONE,
    NON_POSITIVE_PRICE,
    OHLC_INCONSISTENT,     // high < low, or high below body, or low above body
    NEGATIVE_VOLUME,
    NEGATIVE_SPREAD,
    FUTURE_DATED,
    IMPLAUSIBLE_TIME,      // absurdly old
    MISSING_FIELDS,
};

inline const char* toString(BarDefect defect) noexcept {
    switch (defect) {
        case BarDefect::NONE:               return "NONE";
        case BarDefect::NON_POSITIVE_PRICE: return "NON_POSITIVE_PRICE";
        case BarDefect::OHLC_INCONSISTENT:  return "OHLC_INCONSISTENT";
        case BarDefect::NEGATIVE_VOLUME:    return "NEGATIVE_VOLUME";
        case BarDefect::NEGATIVE_SPREAD:    return "NEGATIVE_SPREAD";
        case BarDefect::FUTURE_DATED:       return "FUTURE_DATED";
        case BarDefect::IMPLAUSIBLE_TIME:   return "IMPLAUSIBLE_TIME";
        case BarDefect::MISSING_FIELDS:     return "MISSING_FIELDS";
    }
    return "MISSING_FIELDS";
}

struct BarValidation {
    bool ok = false;
    DataQualityState quality = DataQualityState::UNKNOWN;
    BarDefect defect = BarDefect::MISSING_FIELDS;
    std::string detail;
};

struct SequenceValidation {
    bool ok = false;
    DataQualityState quality = DataQualityState::UNKNOWN;
    std::vector<std::string> issues;
};

struct ValidationPolicy {
    // A bar may not open later than now + skew.
    std::int64_t futureSkewMillis = 5 * 60 * 1000;
    // A bar older than this relative to now is IMPLAUSIBLE.
    std::int64_t maxAgeMillis = 400LL * 24 * 60 * 60 * 1000;
    bool requireStrictlyIncreasingTime = true;
};

class DataValidator {
public:
    DataValidator() = default;
    explicit DataValidator(ValidationPolicy policy) : policy_(policy) {}

    const ValidationPolicy& policy() const noexcept { return policy_; }

    BarValidation validateBar(const Bar& bar, Timestamp now) const;

    // Validates ordering/uniqueness of a bar sequence. Expected to be sorted
    // ascending by openTime. Duplicates and out-of-order entries are reported.
    SequenceValidation validateSequence(const std::vector<Bar>& bars) const;

private:
    ValidationPolicy policy_;
};

}  // namespace aura
