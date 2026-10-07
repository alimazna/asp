// PKG-0008 - Probability API surface implementation (v1).

#include "api/ProbabilityApi.h"

#include <cmath>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace aura {
namespace {

// Coverage-tier boundaries. These MUST equal TIER_BOUNDS in
// src/models/calibration.py: low [0,1/3), medium [1/3,2/3), high [2/3,1].
constexpr double kLowMax = 1.0 / 3.0;     // medium begins here
constexpr double kMediumMax = 2.0 / 3.0;  // high begins here

// The producer (src/models/api_contract.py) uses "UP"/"DOWN"; the backend uses
// LONG/SHORT. NONE has no probability direction and is reported as-is.
const char* directionToUpDown(SignalDirection d) noexcept {
    switch (d) {
        case SignalDirection::LONG:  return "UP";
        case SignalDirection::SHORT: return "DOWN";
        case SignalDirection::NONE:  return "NONE";
    }
    return "NONE";
}

std::string isoUtc(std::int64_t seconds) {
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

}  // namespace

const char* probabilityTier(double probability) noexcept {
    if (!(probability >= 0.0 && probability <= 1.0)) return nullptr;
    if (probability < kLowMax) return "low";
    if (probability < kMediumMax) return "medium";
    return "high";
}

bool probabilityTierBoundariesMatchProducer() noexcept {
    return kLowMax == 1.0 / 3.0 && kMediumMax == 2.0 / 3.0 &&
           std::string(probabilityTier(0.0)) == "low" &&
           std::string(probabilityTier(kLowMax)) == "medium" &&
           std::string(probabilityTier(kMediumMax)) == "high" &&
           std::string(probabilityTier(1.0)) == "high" &&
           probabilityTier(-0.01) == nullptr &&
           probabilityTier(1.01) == nullptr;
}

ApiResponse ProbabilityApi::latest(const PredictionLedger* ledger) const {
    if (ledger == nullptr) {
        return errorResponse(503, "dependency_unavailable",
                             "backend component unavailable: prediction ledger");
    }
    const std::vector<PredictionRecord>& records = ledger->records();
    if (records.empty()) {
        std::vector<ApiField> fields;
        fields.push_back({"available", jsonBool(false), true});
        fields.push_back({"reason", "no predictions recorded yet"});
        return ApiResponse{200, "application/json", envelope(jsonObject(fields)),
                           true};
    }

    const PredictionRecord& record = records.back();
    const bool directionValid = record.direction == SignalDirection::LONG ||
                                record.direction == SignalDirection::SHORT;
    const bool inRange = probabilityTier(record.probabilityEstimate) != nullptr;
    // RULE C: presentable as a probability ONLY when calibrated (and audited),
    // directional, and in range. Anything else stays a score.
    const bool presentable =
        audited_ && record.probabilityCalibrated && directionValid && inRange;

    std::vector<ApiField> fields;
    fields.push_back({"available", jsonBool(true), true});
    fields.push_back({"calibrated", jsonBool(presentable), true});

    if (presentable) {
        const double p = record.probabilityEstimate;
        fields.push_back({"probability", jsonNumber(p), true});
        // No interval/model version is sourced yet; report them as absent rather
        // than inventing a value.
        fields.push_back({"confidence_interval", "null", true});
        fields.push_back({"coverage_tier", probabilityTier(p)});
        fields.push_back({"model_version", "null", true});
    } else {
        fields.push_back({"probability", "null", true});
        fields.push_back({"confidence_interval", "null", true});
        fields.push_back({"coverage_tier", "null", true});
        fields.push_back({"model_version", "null", true});
        if (!audited_ || !record.probabilityCalibrated) {
            fields.push_back({"note", "uncalibrated: not a probability (RULE C)"});
        } else if (!directionValid) {
            fields.push_back({"note", "calibrated but direction is NONE"});
        } else {
            fields.push_back({"note", "calibrated value out of range [0,1]"});
        }
    }

    // The uncalibrated score is always carried, labelled honestly as a score.
    fields.push_back({"score", jsonNumber(record.score), true});
    fields.push_back({"score_is_probability", jsonBool(false), true});
    fields.push_back({"direction", directionToUpDown(record.direction)});
    fields.push_back({"timestamp", isoUtc(record.asOfBarOpenSec)});
    fields.push_back({"decision_id", record.decisionId.value()});
    fields.push_back({"trigger_timeframe", toString(record.timeframe)});
    fields.push_back({"as_of_bar_open_sec", jsonInteger(record.asOfBarOpenSec), true});
    fields.push_back({"shadow_only", jsonBool(true), true});
    return ApiResponse{200, "application/json", envelope(jsonObject(fields)), true};
}

}  // namespace aura
