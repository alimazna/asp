// Defensive analysis parser/validator implementation.

#include "news/NewsAnalysisParser.h"

#include "foundation/Json.h"

#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace aura {
namespace {

bool isAllowedDirection(const std::string& d) {
    return d == "bullish" || d == "bearish" || d == "sideways" ||
           d == "no_trade";
}

bool isAllowedKind(const std::string& k) {
    return k == "fact" || k == "forecast" || k == "commentary" ||
           k == "unverified";
}

bool isAllowedGoldRelevance(const std::string& v) {
    return v == "high" || v == "medium" || v == "low" || v == "none" ||
           v == "unknown";
}

bool isAllowedAgreement(const std::string& a) {
    return a == "agree" || a == "disagree" || a == "unknown";
}

bool isFinitePositive(double v) { return std::isfinite(v) && v > 0.0; }

// Bounds on model-supplied text. A model that emits a multi-megabyte field or
// a huge array would otherwise be persisted verbatim; these caps keep a single
// analysis within what the store and the mobile client can handle. They are
// validation limits, not content filters: an over-long field is rejected rather
// than truncated (truncation would silently alter the model's meaning).
constexpr std::size_t kMaxTextBytes = 64 * 1024;
constexpr std::size_t kMaxArrayItems = 256;

bool withinTextLimit(const std::string& s) { return s.size() <= kMaxTextBytes; }

bool withinArrayLimit(const std::vector<std::string>& v) {
    return v.size() <= kMaxArrayItems;
}

std::vector<std::string> readStringArray(const JsonValue& value) {
    std::vector<std::string> out;
    if (!value.isArray()) return out;
    for (const auto& item : value.items()) {
        if (item.isString()) out.push_back(item.asString());
    }
    return out;
}

}  // namespace

bool extractFirstJsonObject(const std::string& text, std::string& out) {
    const std::size_t start = text.find('{');
    if (start == std::string::npos) return false;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                out = text.substr(start, i - start + 1);
                return true;
            }
        }
    }
    return false;
}

ParsedAnalysis parseAnalysisResponse(const std::string& text) {
    ParsedAnalysis result;

    std::string jsonText;
    if (!extractFirstJsonObject(text, jsonText)) {
        result.error = "no JSON object found in model output";
        return result;
    }
    JsonValue root;
    std::string parseError;
    if (!JsonValue::parse(jsonText, root, parseError)) {
        result.error = "model output JSON is malformed: " + parseError;
        return result;
    }
    if (!root.isObject()) {
        result.error = "model output JSON is not an object";
        return result;
    }

    NewsAnalysis a;

    // Required: summary_ar (non-empty string, bounded).
    a.summaryAr = root["summary_ar"].asString();
    if (a.summaryAr.empty()) {
        result.error = "missing required field: summary_ar";
        return result;
    }
    if (!withinTextLimit(a.summaryAr)) {
        result.error = "summary_ar exceeds size limit";
        return result;
    }

    // Required: direction, restricted to the four allowed values.
    a.proposedDirection = root["direction"].asString();
    if (!isAllowedDirection(a.proposedDirection)) {
        result.error = "invalid direction: '" + a.proposedDirection + "'";
        return result;
    }

    // Required: astra_agreement, restricted.
    a.astraAgreement = root["astra_agreement"].asString();
    if (a.astraAgreement.empty()) a.astraAgreement = "unknown";
    if (!isAllowedAgreement(a.astraAgreement)) {
        result.error = "invalid astra_agreement: '" + a.astraAgreement + "'";
        return result;
    }

    // Optional: the model's own gold-relevance judgement. Unknown/absent is
    // preserved as "unknown" rather than defaulted to a relevance the model
    // never asserted.
    a.goldRelevance = root["gold_relevance"].asString();
    if (a.goldRelevance.empty()) a.goldRelevance = "unknown";
    if (!isAllowedGoldRelevance(a.goldRelevance)) {
        result.error = "invalid gold_relevance: '" + a.goldRelevance + "'";
        return result;
    }
    // Optional free-text fields.
    a.goldImpactAr = root["gold_impact_ar"].asString();
    a.invalidatingConditionsAr = root["invalidating_conditions_ar"].asString();
    a.disagreementNoteAr = root["disagreement_note_ar"].asString();
    a.marketImplicationsAr = readStringArray(root["market_implications_ar"]);
    a.supportingEvidenceAr = readStringArray(root["supporting_evidence_ar"]);
    a.opposingEvidenceAr = readStringArray(root["opposing_evidence_ar"]);
    a.limitationsAr = readStringArray(root["limitations_ar"]);

    // Bounds: a model that emits an oversized free-text field or a runaway
    // array is rejected, so nothing unbounded is persisted.
    if (!withinTextLimit(a.goldImpactAr) ||
        !withinTextLimit(a.invalidatingConditionsAr) ||
        !withinTextLimit(a.disagreementNoteAr) ||
        !withinArrayLimit(a.marketImplicationsAr) ||
        !withinArrayLimit(a.supportingEvidenceAr) ||
        !withinArrayLimit(a.opposingEvidenceAr) ||
        !withinArrayLimit(a.limitationsAr)) {
        result.error = "analysis field exceeds size limit";
        return result;
    }

    // Facts: each element must be an object with text + allowed kind.
    const JsonValue& facts = root["facts"];
    if (facts.isArray()) {
        if (facts.items().size() > kMaxArrayItems) {
            result.error = "facts[] exceeds size limit";
            return result;
        }
        for (const auto& item : facts.items()) {
            if (!item.isObject()) {
                result.error = "facts[] element is not an object";
                return result;
            }
            NewsFact f;
            f.text = item["text"].asString();
            f.kind = item["kind"].asString();
            if (f.text.empty()) {
                result.error = "facts[] element has empty text";
                return result;
            }
            if (!withinTextLimit(f.text)) {
                result.error = "facts[] element exceeds size limit";
                return result;
            }
            if (!isAllowedKind(f.kind)) {
                result.error = "facts[] element has invalid kind: '" + f.kind +
                               "'";
                return result;
            }
            a.facts.push_back(std::move(f));
        }
    }

    // Levels: only honoured for an actionable direction, and only when finite
    // and positive. A level on a no_trade/sideways proposal is dropped rather
    // than shown (it would imply an order that does not exist).
    const bool actionable = (a.proposedDirection == "bullish" ||
                             a.proposedDirection == "bearish");
    const JsonValue& entry = root["entry"];
    const JsonValue& stop = root["stop_loss"];
    const JsonValue& target = root["take_profit"];
    if (actionable) {
        if (entry.isNumber() && isFinitePositive(entry.asDouble())) {
            a.hasEntry = true;
            a.entry = entry.asDouble();
        }
        if (stop.isNumber() && isFinitePositive(stop.asDouble())) {
            a.hasStop = true;
            a.stopLoss = stop.asDouble();
        }
        if (target.isNumber() && isFinitePositive(target.asDouble())) {
            a.hasTarget = true;
            a.takeProfit = target.asDouble();
        }
    }

    a.status = "ok";
    result.valid = true;
    result.analysis = std::move(a);
    return result;
}

}  // namespace aura
