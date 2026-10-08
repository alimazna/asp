// PKG-0008 - Probability API surface implementation (v1).

#include "api/ProbabilityApi.h"

#include <cmath>
#include <cstdint>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace aura {
namespace {

std::string lowerCopy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A line of the report containing `label` (e.g. "Verdict:"), lowercased.
std::string reportLine(const std::string& content, const std::string& label) {
    std::istringstream stream(content);
    std::string line;
    const std::string needle = lowerCopy(label);
    while (std::getline(stream, line)) {
        if (lowerCopy(line).find(needle) != std::string::npos) return line;
    }
    return "";
}

std::string reportValue(const std::string& content, const std::string& label) {
    const std::string line = reportLine(content, label);
    if (line.empty()) return "";
    const std::size_t at = line.find(':');
    if (at == std::string::npos) return "";
    std::string value = line.substr(at + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '*')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

// First alphabetic word, lowercased, with leading markdown/punctuation and
// surrounding decoration stripped. The verdict's leading token is the decision
// ("PASS ...", "NOT PASS", "FAIL ..."); a substring scan would wrongly accept a
// non-PASS verdict whose prose merely contains the letters "pass".
std::string leadingWord(const std::string& s) {
    std::string out;
    bool started = false;
    for (char c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalpha(uc)) {
            out.push_back(static_cast<char>(std::tolower(uc)));
            started = true;
        } else if (started) {
            break;
        }
    }
    return out;
}

// "NOT authorised" / "not authorized" -> no publication authorisation.
bool withholdsPublication(const std::string& line) {
    return line.find("not authoris") != std::string::npos ||
           line.find("not authoriz") != std::string::npos ||
           line.find("withheld") != std::string::npos ||
           line.find("withhold") != std::string::npos ||
           line.find("no publication") != std::string::npos;
}

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

ProbabilityApi::Gate ProbabilityApi::evaluate(
    const PredictionRecord& record) const {
    Gate g;
    const bool directionValid = record.direction == SignalDirection::LONG ||
                                record.direction == SignalDirection::SHORT;
    const bool inRange = probabilityTier(record.probabilityEstimate) != nullptr;
    g.direction = directionToUpDown(record.direction);
    g.score = record.score;
    // RULE C: presentable as a probability ONLY when calibrated (and audited),
    // directional, and in range. Anything else stays a score.
    g.presentable = audited_ && record.probabilityCalibrated && directionValid &&
                    inRange;
    if (g.presentable) {
        g.probability = record.probabilityEstimate;
        g.tier = probabilityTier(g.probability);
    } else if (!audited_ || !record.probabilityCalibrated) {
        g.reason = "uncalibrated: not a probability (RULE C)";
    } else if (!directionValid) {
        g.reason = "calibrated but direction is NONE";
    } else {
        g.reason = "calibrated value out of range [0,1]";
    }
    return g;
}

ProbabilityApi::View ProbabilityApi::view(const PredictionLedger* ledger) const {
    View v;
    if (ledger == nullptr) return v;
    const std::vector<PredictionRecord>& records = ledger->records();
    if (records.empty()) return v;
    v.available = true;
    v.record = records.back();
    v.gate = evaluate(v.record);
    return v;
}

ApiResponse ProbabilityApi::latest(const PredictionLedger* ledger) const {
    if (ledger == nullptr) {
        return errorResponse(503, "dependency_unavailable",
                             "backend component unavailable: prediction ledger");
    }
    const View v = view(ledger);
    if (!v.available) {
        std::vector<ApiField> fields;
        fields.push_back({"available", jsonBool(false), true});
        fields.push_back({"reason", "no predictions recorded yet"});
        return ApiResponse{200, "application/json", envelope(jsonObject(fields)),
                           true};
    }

    const PredictionRecord& record = v.record;
    std::vector<ApiField> fields;
    fields.push_back({"available", jsonBool(true), true});
    fields.push_back({"calibrated", jsonBool(v.gate.presentable), true});

    if (v.gate.presentable) {
        fields.push_back({"probability", jsonNumber(v.gate.probability), true});
        // No interval/model version is sourced yet; report them as absent rather
        // than inventing a value.
        fields.push_back({"confidence_interval", "null", true});
        fields.push_back({"coverage_tier", v.gate.tier});
        fields.push_back({"model_version", "null", true});
    } else {
        fields.push_back({"probability", "null", true});
        fields.push_back({"confidence_interval", "null", true});
        fields.push_back({"coverage_tier", "null", true});
        fields.push_back({"model_version", "null", true});
        fields.push_back({"note", v.gate.reason});
    }

    // The uncalibrated score is always carried, labelled honestly as a score.
    fields.push_back({"score", jsonNumber(record.score), true});
    fields.push_back({"score_is_probability", jsonBool(false), true});
    fields.push_back({"direction", v.gate.direction});
    fields.push_back({"timestamp", isoUtcSeconds(record.asOfBarOpenSec)});
    fields.push_back({"decision_id", record.decisionId.value()});
    fields.push_back({"trigger_timeframe", toString(record.timeframe)});
    fields.push_back({"as_of_bar_open_sec", jsonInteger(record.asOfBarOpenSec), true});
    fields.push_back({"shadow_only", jsonBool(true), true});
    return ApiResponse{200, "application/json", envelope(jsonObject(fields)), true};
}

ProbabilityApi::Audit ProbabilityApi::applyCalibrationAudit(const std::string& path) {
    Audit audit;
    audit.source = path;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        // No artifact: the gate stays closed. Honest absence, not a silent pass.
        audit.reason = "calibration audit artifact not found: " + path;
        audited_ = false;
        audit_ = audit;
        return audit;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string content = buffer.str();
    audit.present = true;

    const std::string verdict = reportValue(content, "Verdict:");
    const std::string date = reportValue(content, "Date:");
    const std::string auditor = reportValue(content, "Auditor:");
    audit.date = date;
    audit.auditor = auditor;

    // Explicit "NOT AUTHORISED" (or a withheld authorisation) anywhere relevant.
    const std::string verdictLower = lowerCopy(verdict);
    const std::string caveatLower = lowerCopy(reportLine(content, "publication"));
    const bool withheld = withholdsPublication(verdictLower) ||
                          withholdsPublication(caveatLower) ||
                          withholdsPublication(lowerCopy(content));

    // F17-0: the verdict's LEADING token decides, not a substring. "PASS",
    // "PASS (methodology)", "PASSED" open the gate; "NOT PASS", "FAIL (did not
    // pass)", "PASSING", "NOT PASSING" must not.
    const std::string verdictToken = leadingWord(verdict);
    audit.passed = verdictToken == "pass" || verdictToken == "passed";
    // A PASS that withholds publication (synthetic data, E05) does NOT open the
    // probability gate: the value remains a score. Only a PASS that authorises
    // publication unlocks a calibrated probability.
    audit.publicationAuthorised = audit.passed && !withheld;
    audit.reason = audit.publicationAuthorised
                       ? "publication authorised"
                       : (audit.passed ? "PASS but publication not authorised"
                                       : "verdict is not PASS");

    audited_ = audit.publicationAuthorised;
    return audit;
}

}  // namespace aura
