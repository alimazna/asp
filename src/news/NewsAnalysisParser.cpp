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

// The channel through which news reaches gold. A closed vocabulary so the
// client can group without parsing prose; an out-of-set value is rejected, not
// coerced, exactly as gold_relevance is.
bool isAllowedGoldChannel(const std::string& v) {
    return v == "monetary_policy" || v == "inflation" || v == "usd_rates" ||
           v == "safe_haven" || v == "physical_demand" ||
           v == "positioning" || v == "other" || v == "unknown";
}

// How soon the effect is expected. Qualitative on purpose: a horizon in minutes
// would be a fabricated precision.
bool isAllowedImpactHorizon(const std::string& v) {
    return v == "immediate" || v == "short" || v == "medium" || v == "long" ||
           v == "unknown";
}

bool isAllowedAgreement(const std::string& a) {
    return a == "agree" || a == "disagree" || a == "unknown";
}

// Tone: a symmetric five-point scale. Symmetric on purpose so a negative
// reading is not compressed into one bucket while positives get three, which
// would itself bias what the analyst sees.
bool isAllowedTone(const std::string& v) {
    return v == "very_negative" || v == "negative" || v == "neutral" ||
           v == "positive" || v == "very_positive" || v == "unknown";
}

// How strongly the tone is carried. A strong tone with no quotable wording is
// the case tone_evidence_ar exists to expose.
bool isAllowedToneStrength(const std::string& v) {
    return v == "weak" || v == "moderate" || v == "strong" || v == "unknown";
}

// The register of the outlet: an official release reads differently from a
// wire report or an opinion column, and that difference is signal.
bool isAllowedSourceRegister(const std::string& v) {
    return v == "official" || v == "wire" || v == "analytical" ||
           v == "opinion" || v == "other" || v == "unknown";
}

// How much the language overstates the underlying facts.
bool isAllowedSensationalism(const std::string& v) {
    return v == "none" || v == "low" || v == "medium" || v == "high" ||
           v == "unknown";
}

// Whether the story is genuinely new information or a re-telling.
bool isAllowedNovelty(const std::string& v) {
    return v == "new" || v == "partly_priced" || v == "already_priced" ||
           v == "unknown";
}

// How much the SOURCE itself hedges. Distinct from our own confidence.
bool isAllowedUncertainty(const std::string& v) {
    return v == "low" || v == "medium" || v == "high" || v == "unknown";
}

bool isFinitePositive(double v) { return std::isfinite(v) && v > 0.0; }

// A point-distance is honest where a price would be fabricated, but it still
// has to be plausible: a whole, positive, modest number. The cap rejects a
// runaway value (or a model that echoed a price into the distance field) while
// leaving room for any real gold move measured in points.
bool isPlausiblePoints(double v) {
    return std::isfinite(v) && v > 0.0 && v <= 100000.0 &&
           v == static_cast<double>(static_cast<int>(v));
}

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

// A level is plausible only if it sits near the market the model was shown.
// Without a reference price there is nothing to be near, so every level is
// dropped. With one, the tolerance is a multiple of the realised bar range
// (ATR); when no ATR is known, a fraction of the price itself is used as a
// coarse band. The bands are deliberately wide enough for a legitimate swing
// stop but far too tight to admit a number invented out of thin air.
bool levelNearMarket(double level, const PriceGrounding& g) {
    if (!isFinitePositive(level)) return false;
    // Legacy lenient path: no market reference to check against.
    if (!g.strict) return true;
    // Strict path: with no real reference price there is nothing to be near,
    // so an ungrounded level is dropped rather than trusted.
    if (!isFinitePositive(g.referencePrice)) return false;
    const double ref = g.referencePrice;
    double tolerance = g.atr * (g.maxDeviationBars > 0 ? g.maxDeviationBars : 8.0);
    if (!(tolerance > 0.0)) tolerance = ref * 0.05;  // 5% band, no ATR known
    return std::fabs(level - ref) <= tolerance;
}

ParsedAnalysis parseAnalysisResponse(const std::string& text) {
    // The ungrounded overload has no market to check against, so it keeps the
    // original lenient behaviour. Callers that do hold real candles use the
    // grounded overload below, which is what the live pipeline calls.
    return parseAnalysisResponse(text, PriceGrounding{});
}

ParsedAnalysis parseAnalysisResponse(const std::string& text,
                                     const PriceGrounding& grounding) {
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

    // Optional: the gold transmission channel and the impact horizon. Both are
    // closed vocabularies; absent/empty becomes "unknown", an out-of-set value
    // is rejected rather than coerced to a channel the model never named.
    a.goldChannel = root["gold_channel"].asString();
    if (a.goldChannel.empty()) a.goldChannel = "unknown";
    if (!isAllowedGoldChannel(a.goldChannel)) {
        result.error = "invalid gold_channel: '" + a.goldChannel + "'";
        return result;
    }
    a.impactHorizon = root["impact_horizon"].asString();
    if (a.impactHorizon.empty()) a.impactHorizon = "unknown";
    if (!isAllowedImpactHorizon(a.impactHorizon)) {
        result.error = "invalid impact_horizon: '" + a.impactHorizon + "'";
        return result;
    }

    // Tone, framing and subtext classifiers. Each is optional in the sense that
    // an absent value is honestly "unknown", but a PRESENT value must be in the
    // closed vocabulary: coercing an unrecognised tone to "neutral" would put a
    // reading the model never made in front of the user, which is exactly the
    // failure the classifier exists to prevent.
    a.tone = root["tone"].asString();
    if (a.tone.empty()) a.tone = "unknown";
    if (!isAllowedTone(a.tone)) {
        result.error = "invalid tone: '" + a.tone + "'";
        return result;
    }
    a.toneStrength = root["tone_strength"].asString();
    if (a.toneStrength.empty()) a.toneStrength = "unknown";
    if (!isAllowedToneStrength(a.toneStrength)) {
        result.error = "invalid tone_strength: '" + a.toneStrength + "'";
        return result;
    }
    a.sourceRegister = root["source_register"].asString();
    if (a.sourceRegister.empty()) a.sourceRegister = "unknown";
    if (!isAllowedSourceRegister(a.sourceRegister)) {
        result.error = "invalid source_register: '" + a.sourceRegister + "'";
        return result;
    }
    a.sensationalism = root["sensationalism"].asString();
    if (a.sensationalism.empty()) a.sensationalism = "unknown";
    if (!isAllowedSensationalism(a.sensationalism)) {
        result.error = "invalid sensationalism: '" + a.sensationalism + "'";
        return result;
    }
    a.novelty = root["novelty"].asString();
    if (a.novelty.empty()) a.novelty = "unknown";
    if (!isAllowedNovelty(a.novelty)) {
        result.error = "invalid novelty: '" + a.novelty + "'";
        return result;
    }
    a.uncertainty = root["uncertainty"].asString();
    if (a.uncertainty.empty()) a.uncertainty = "unknown";
    if (!isAllowedUncertainty(a.uncertainty)) {
        result.error = "invalid uncertainty: '" + a.uncertainty + "'";
        return result;
    }
    // The tone must be attributable. A non-neutral tone with no quoted wording
    // is downgraded to "unknown" rather than displayed, because an unattributed
    // tone is an assertion the reader cannot check -- the same standard applied
    // to an ungrounded price. "neutral"/"unknown" need no evidence: they claim
    // nothing.
    a.toneEvidenceAr = root["tone_evidence_ar"].asString();
    if (a.tone != "neutral" && a.tone != "unknown" && a.toneEvidenceAr.empty()) {
        a.tone = "unknown";
        a.toneStrength = "unknown";
    }
    a.hiddenContextAr = root["hidden_context_ar"].asString();
    a.framingBiasAr = root["framing_bias_ar"].asString();
    if (!withinTextLimit(a.toneEvidenceAr) ||
        !withinTextLimit(a.hiddenContextAr) ||
        !withinTextLimit(a.framingBiasAr)) {
        result.error = "tone field exceeds size limit";
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
    // than shown (it would imply an order that does not exist). Every surviving
    // level must also sit near the real market (see levelNearMarket): a small
    // model that ignores the prompt and invents a number has that number
    // dropped here, so no fabricated level is ever persisted or displayed.
    const bool actionable = (a.proposedDirection == "bullish" ||
                             a.proposedDirection == "bearish");
    const JsonValue& entry = root["entry"];
    const JsonValue& stop = root["stop_loss"];
    const JsonValue& target = root["take_profit"];
    if (actionable) {
        if (entry.isNumber() && levelNearMarket(entry.asDouble(), grounding)) {
            a.hasEntry = true;
            a.entry = entry.asDouble();
        }
        if (stop.isNumber() && levelNearMarket(stop.asDouble(), grounding)) {
            a.hasStop = true;
            a.stopLoss = stop.asDouble();
        }
        if (target.isNumber() && levelNearMarket(target.asDouble(), grounding)) {
            a.hasTarget = true;
            a.takeProfit = target.asDouble();
        }
    }

    // Relative, price-free guidance. Unlike entry/stop/target these need no
    // market reference: a distance is not a price, so it cannot be a fabricated
    // level. They are honoured only for an actionable direction and only as a
    // bounded positive integer, so a runaway or negative value is dropped.
    const JsonValue& move = root["expected_move_points"];
    const JsonValue& stopDist = root["stop_distance_points"];
    if (actionable) {
        if (move.isNumber() && isPlausiblePoints(move.asDouble())) {
            a.hasExpectedMove = true;
            a.expectedMovePoints = static_cast<int>(move.asDouble());
        }
        if (stopDist.isNumber() && isPlausiblePoints(stopDist.asDouble())) {
            a.hasStopDistance = true;
            a.stopDistancePoints = static_cast<int>(stopDist.asDouble());
        }
    }
    a.relativeLevelsNoteAr = root["relative_levels_note_ar"].asString();
    if (!withinTextLimit(a.relativeLevelsNoteAr)) {
        result.error = "relative_levels_note_ar exceeds size limit";
        return result;
    }

    a.status = "ok";
    result.valid = true;
    result.analysis = std::move(a);
    return result;
}

}  // namespace aura
