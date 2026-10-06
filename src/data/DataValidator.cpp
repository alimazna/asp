// DAT-0009 - Data validation implementation.

#include "data/DataValidator.h"

#include <cmath>

namespace aura {

BarValidation DataValidator::validateBar(const Bar& bar, Timestamp now) const {
    BarValidation result;

    const bool pricesPositive =
        bar.open > 0.0 && bar.high > 0.0 && bar.low > 0.0 && bar.close > 0.0;
    if (!pricesPositive) {
        result.defect = BarDefect::NON_POSITIVE_PRICE;
        result.detail = "one or more OHLC prices are non-positive";
        result.quality = DataQualityState::INVALID;
        return result;
    }

    const bool finitePrices =
        std::isfinite(bar.open) && std::isfinite(bar.high) &&
        std::isfinite(bar.low) && std::isfinite(bar.close);
    if (!finitePrices) {
        result.defect = BarDefect::OHLC_INCONSISTENT;
        result.detail = "non-finite OHLC value";
        result.quality = DataQualityState::INVALID;
        return result;
    }

    const double bodyHigh = std::max(bar.open, bar.close);
    const double bodyLow = std::min(bar.open, bar.close);
    if (bar.high < bar.low || bar.high < bodyHigh || bar.low > bodyLow) {
        result.defect = BarDefect::OHLC_INCONSISTENT;
        result.detail = "OHLC ordering violated (high/low do not bound the body)";
        result.quality = DataQualityState::INVALID;
        return result;
    }

    if (bar.tickVolume < 0 || bar.realVolume < 0) {
        result.defect = BarDefect::NEGATIVE_VOLUME;
        result.detail = "negative volume";
        result.quality = DataQualityState::INVALID;
        return result;
    }
    if (bar.spread < 0) {
        result.defect = BarDefect::NEGATIVE_SPREAD;
        result.detail = "negative spread";
        result.quality = DataQualityState::INVALID;
        return result;
    }

    if (bar.openTimeSec <= 0) {
        result.defect = BarDefect::IMPLAUSIBLE_TIME;
        result.detail = "non-positive bar open time";
        result.quality = DataQualityState::INVALID;
        return result;
    }

    if (now.isKnown()) {
        const std::int64_t openMillis = bar.openTimeSec * 1000;
        if (openMillis > now.epochMillis() + policy_.futureSkewMillis) {
            result.defect = BarDefect::FUTURE_DATED;
            result.detail = "bar opens in the future";
            result.quality = DataQualityState::INVALID;
            return result;
        }
        if (now.epochMillis() - openMillis > policy_.maxAgeMillis) {
            result.defect = BarDefect::IMPLAUSIBLE_TIME;
            result.detail = "bar is implausibly old";
            result.quality = DataQualityState::STALE;
            return result;
        }
    }

    result.ok = true;
    result.defect = BarDefect::NONE;
    result.quality = DataQualityState::VALID;
    result.detail = "ok";
    return result;
}

SequenceValidation DataValidator::validateSequence(
    const std::vector<Bar>& bars) const {
    SequenceValidation result;
    if (bars.empty()) {
        result.quality = DataQualityState::MISSING;
        result.issues.push_back("empty sequence");
        return result;
    }

    bool sawOutOfOrder = false;
    bool sawDuplicate = false;
    for (std::size_t i = 1; i < bars.size(); ++i) {
        const std::int64_t prev = bars[i - 1].openTimeSec;
        const std::int64_t curr = bars[i].openTimeSec;
        if (curr == prev) {
            sawDuplicate = true;
            result.issues.push_back("duplicate open time " + std::to_string(curr));
        } else if (curr < prev) {
            sawOutOfOrder = true;
            result.issues.push_back("out-of-order open time " + std::to_string(curr) +
                                    " after " + std::to_string(prev));
        }
    }

    if (sawOutOfOrder) {
        result.quality = DataQualityState::OUT_OF_ORDER;
        return result;
    }
    if (sawDuplicate) {
        result.quality = DataQualityState::DUPLICATE;
        return result;
    }
    result.ok = true;
    result.quality = DataQualityState::VALID;
    return result;
}

}  // namespace aura
