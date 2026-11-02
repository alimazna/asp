// AYZR-ORC-001 — Central Orchestrator (implementation).

#include "ayzr/ecosystem/CentralOrchestrator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace aura {
namespace ayzr {
namespace eco {

const char* toString(DecisionState s) noexcept {
    switch (s) {
        case DecisionState::PUBLISH:             return "PUBLISH";
        case DecisionState::PUBLISH_WITH_CAUTION:return "PUBLISH_WITH_CAUTION";
        case DecisionState::WATCH:               return "WATCH";
        case DecisionState::WITHHOLD:            return "WITHHOLD";
    }
    return "WITHHOLD";
}

const char* decisionStateLabelAr(DecisionState s) noexcept {
    switch (s) {
        case DecisionState::PUBLISH:             return "منشور";
        case DecisionState::PUBLISH_WITH_CAUTION:return "منشور مع تحذير";
        case DecisionState::WATCH:               return "مراقبة";
        case DecisionState::WITHHOLD:            return "محجوب";
    }
    return "محجوب";
}

std::string CentralOrchestrator::makeOpportunityId(const std::string& symbol,
                                                   Timeframe tf,
                                                   EngineDirection dir,
                                                   std::int64_t anchorBarUtc) {
    // Deterministic over the setup identity. Uses the same FNV-1a mixing as
    // makeEventId so ids are stable across runs and restarts.
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](const std::string& s) {
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    };
    mix(symbol); mix("|"); mix(toString(tf)); mix("|"); mix(toString(dir));
    mix("|"); mix(std::to_string(anchorBarUtc));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return std::string("op-") + buf;
}

double CentralOrchestrator::weightFor(StrategyFamily f) const {
    for (const auto& w : cfg_.weights) if (w.family == f) return w.weight;
    return 0.5;   // an unlisted family is trusted least, never guessed as 1.0
}

AgreementScore CentralOrchestrator::scoreDirection(
    const std::vector<EngineResult>& group, EngineDirection dir) const {
    AgreementScore s;
    for (const auto& e : group) {
        // Only decision-grade, publishable-ish events count toward agreement.
        if (e.direction == EngineDirection::NO_TRADE) continue;
        if (e.dataQuality != DataQualityState::VALID) continue;
        if (e.status == EventStatus::WITHHELD || e.status == EventStatus::EXPIRED ||
            e.status == EventStatus::INVALIDATED) continue;
        const double w = weightFor(e.strategyFamily);
        if (e.direction == dir) { s.score += w; s.agreeing.push_back(e.engineId); }
        else if (e.direction != EngineDirection::SIDEWAYS) {
            s.opposingScore += w; s.opposing.push_back(e.engineId);
        }
    }
    return s;
}

CycleResult CentralOrchestrator::runCycle(const std::vector<EngineResult>& events,
                                          std::int64_t nowUtc,
                                          const std::string& correlationId) {
    CycleResult out;
    out.asOfUtc = nowUtc;
    out.correlationId = correlationId;

    // ---- Step 2: deduplicate by event id (keep the newest per id) -----------
    std::map<std::string, EngineResult> byId;
    for (const auto& e : events) {
        if (e.eventId.empty()) continue;   // an event without identity is unusable
        auto it = byId.find(e.eventId);
        if (it == byId.end() || e.updatedAt >= it->second.updatedAt) {
            byId[e.eventId] = e;
        }
    }

    // ---- Step 1: group by (symbol, timeframe, anchor bar) -------------------
    // Direction is deliberately NOT part of the grouping key: opposing events on
    // the same setup must land in the same group so the conflict is detected.
    struct Key {
        std::string symbol; Timeframe tf; std::int64_t anchor;
        bool operator<(const Key& o) const {
            if (symbol != o.symbol) return symbol < o.symbol;
            if (tf != o.tf) return tf < o.tf;
            return anchor < o.anchor;
        }
    };
    std::map<Key, std::vector<EngineResult>> groups;
    for (const auto& kv : byId) {
        const EngineResult& e = kv.second;
        if (e.direction == EngineDirection::NO_TRADE &&
            e.status != EventStatus::WARNING) {
            continue;   // pure NO_TRADE noise does not form an opportunity
        }
        groups[{e.symbol, e.timeframe, e.dataTimestamp}].push_back(e);
    }

    // ---- Steps 3-6: per group ----------------------------------------------
    for (auto& kv : groups) {
        const std::vector<EngineResult>& group = kv.second;
        Opportunity op;
        op.symbol = kv.first.symbol;
        op.timeframe = kv.first.tf;
        op.anchorBarUtc = kv.first.anchor;
        op.createdAt = nowUtc;
        op.updatedAt = nowUtc;
        op.contributingEvents = group;

        // Direction with the strongest weighted agreement.
        std::vector<EngineDirection> dirs = {
            EngineDirection::BULLISH, EngineDirection::BEARISH,
            EngineDirection::SIDEWAYS};
        AgreementScore best;
        EngineDirection bestDir = EngineDirection::NO_TRADE;
        for (EngineDirection d : dirs) {
            AgreementScore s = scoreDirection(group, d);
            if (s.score > best.score) { best = s; bestDir = d; }
        }
        op.direction = bestDir;
        op.agreeingEngines = best.agreeing;
        op.disagreeingEngines = best.opposing;
        op.hasConflict = !best.opposing.empty() && best.score > 0.0;
        if (op.hasConflict) {
            std::string note = "تعارض: ";
            for (std::size_t i = 0; i < best.opposing.size(); ++i) {
                if (i) note += ", ";
                note += best.opposing[i];
            }
            note += " يعارضون الاتجاه الغالب.";
            op.conflictNotesAr.push_back(note);
        }

        // Frozen ASTRA output present?
        for (const auto& e : group) {
            if (e.immutableCoreOutput || e.strategyFamily == StrategyFamily::ASTRA_CORE)
                op.astraCorePresent = true;
            for (const auto& f : e.riskFlags) op.riskFlags.push_back(f);
        }

        // Data-quality gate.
        bool anyNonValid = false;
        for (const auto& e : group)
            if (e.dataQuality != DataQualityState::VALID) anyNonValid = true;

        // Choose the level set from the highest-weight agreeing event that has
        // an entry + stop, so provenance is explicit.
        double bestW = -1.0;
        for (const auto& e : group) {
            if (e.direction != bestDir) continue;
            if (!e.entry.has_value() || !e.stopLoss.has_value()) continue;
            const double w = weightFor(e.strategyFamily);
            if (w > bestW) {
                bestW = w;
                op.entry = e.entry;
                op.stopLoss = e.stopLoss;
                op.takeProfitLevels = e.takeProfitLevels;
                op.levelSourceEngine = e.engineId;
                op.expiresAt = e.expiresAt;
            }
        }

        op.opportunityId = makeOpportunityId(op.symbol, op.timeframe,
                                             op.direction, op.anchorBarUtc);

        // ---- Step 5: decision ----------------------------------------------
        DecisionState state;
        std::string reason;
        if (bestDir == EngineDirection::NO_TRADE || best.score <= cfg_.watchAbove) {
            state = DecisionState::WITHHOLD;
            reason = "no_weighted_agreement";
        } else if (cfg_.requireDecisionGradeData && anyNonValid) {
            state = DecisionState::WITHHOLD;
            reason = "non_decision_grade_data";
        } else if (best.score >= cfg_.publishAbove) {
            state = DecisionState::PUBLISH;
            reason = "weighted_agreement_above_publish_threshold";
        } else {
            state = DecisionState::WATCH;
            reason = "weighted_agreement_below_publish_threshold";
        }
        if (state == DecisionState::PUBLISH && op.hasConflict &&
            cfg_.downgradeOnConflict) {
            state = DecisionState::PUBLISH_WITH_CAUTION;
            reason = "conflict_downgrade";
        }
        op.state = state;

        // Rationale: always explain, even when withheld.
        switch (state) {
            case DecisionState::PUBLISH:
                op.rationaleAr = "توافق مرجّح بين المحركات يتجاوز حد النشر.";
                break;
            case DecisionState::PUBLISH_WITH_CAUTION:
                op.rationaleAr = "توافق كافٍ لكن مع تعارض مسجّل؛ يُنشر بتحذير.";
                break;
            case DecisionState::WATCH:
                op.rationaleAr = "توافق جزئي؛ يُراقب دون نشر.";
                break;
            case DecisionState::WITHHOLD:
                op.rationaleAr = anyNonValid
                    ? "جودة البيانات غير صالحة للقرار؛ تم الحجب."
                    : "لا يوجد توافق مرجّح كافٍ؛ تم الحجب.";
                break;
        }

        // ---- Step 6: ledger -------------------------------------------------
        if (ledger_ != nullptr) {
            for (const auto& e : group) {
                LedgerEntry le;
                le.atUtc = nowUtc;
                le.type = LedgerEventType::ENGINE_EVENT;
                le.correlationId = correlationId;
                le.opportunityId = op.opportunityId;
                le.engineId = e.engineId;
                le.reasonCode = e.reasonCodes.empty() ? "engine_event" : e.reasonCodes[0];
                le.detailAr = e.explanationAr;
                ledger_->append(std::move(le));
            }
            LedgerEntry le;
            le.atUtc = nowUtc;
            le.correlationId = correlationId;
            le.opportunityId = op.opportunityId;
            le.reasonCode = reason;
            le.detailAr = op.rationaleAr;
            le.type = state == DecisionState::PUBLISH ||
                              state == DecisionState::PUBLISH_WITH_CAUTION
                          ? LedgerEventType::DECISION_PUBLISHED
                          : (state == DecisionState::WATCH
                                 ? LedgerEventType::DECISION_WATCH
                                 : LedgerEventType::DECISION_WITHHELD);
            ledger_->append(std::move(le));
            if (op.hasConflict) {
                LedgerEntry ce;
                ce.atUtc = nowUtc;
                ce.type = LedgerEventType::CONFLICT_DETECTED;
                ce.correlationId = correlationId;
                ce.opportunityId = op.opportunityId;
                ce.reasonCode = "direction_conflict";
                ce.detailAr = op.conflictNotesAr.empty() ? "" : op.conflictNotesAr[0];
                ledger_->append(std::move(ce));
            }
        }

        out.opportunities.push_back(std::move(op));
    }

    // Surface non-decision-grade inputs as cycle-level warnings.
    for (const auto& e : events) {
        if (e.dataQuality != DataQualityState::VALID && e.dataQuality != DataQualityState::UNKNOWN) {
            out.dataQualityWarnings.push_back(
                e.engineId + ": " + toString(e.dataQuality));
        }
    }
    return out;
}

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
