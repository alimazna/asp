// SHD-0012 - Prediction ledger implementation.

#include "ledger/PredictionLedger.h"

namespace aura {

bool PredictionLedger::append(const PredictionRecord& record) {
    if (!record.valid()) return false;
    if (contains(record.decisionId)) return false;   // append-only, no rewrite
    records_.push_back(record);
    return true;
}

bool PredictionLedger::linkOutcome(const EntityId& decisionId,
                                   const Outcome& outcome) {
    for (auto& record : records_) {
        if (record.decisionId != decisionId) continue;
        if (record.hasOutcome) return false;   // outcome already linked
        record.hasOutcome = true;
        record.outcomeId = outcome.outcomeId;
        record.realizedR = outcome.realizedR;
        record.outcomeClass = outcome.classification;
        return true;
    }
    return false;
}

bool PredictionLedger::contains(const EntityId& decisionId) const {
    for (const auto& record : records_) {
        if (record.decisionId == decisionId) return true;
    }
    return false;
}

bool PredictionLedger::get(const EntityId& decisionId, PredictionRecord& out) const {
    for (const auto& record : records_) {
        if (record.decisionId == decisionId) {
            out = record;
            return true;
        }
    }
    return false;
}

std::vector<PredictionRecord> PredictionLedger::unresolved() const {
    std::vector<PredictionRecord> pending;
    for (const auto& record : records_) {
        if (!record.hasOutcome) pending.push_back(record);
    }
    return pending;
}

}  // namespace aura
