#pragma once
// SHD-0011 - Prediction ledger.
//
// Records every prediction (signal + score + confidence) with its
// deterministic decision identity BEFORE the outcome is known. This is the
// no-lookahead ledger: a prediction is immutable once written, and outcomes
// are linked to it afterwards.

#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"
#include "outcomes/Outcome.h"
#include "signals/SignalEngine.h"

#include <string>
#include <vector>

namespace aura {

struct PredictionRecord {
    EntityId decisionId;
    Timeframe timeframe = Timeframe::M15;
    std::int64_t asOfBarOpenSec = 0;
    SignalDirection direction = SignalDirection::NONE;
    double score = 0.0;
    double confidence = 0.0;
    double probabilityEstimate = 0.0;
    bool probabilityCalibrated = false;
    double referencePrice = 0.0;
    double stopPrice = 0.0;
    double targetPrice = 0.0;
    std::string rationale;
    Timestamp recordedAt;

    // Linked after the fact.
    bool hasOutcome = false;
    EntityId outcomeId;
    double realizedR = 0.0;
    OutcomeClass outcomeClass = OutcomeClass::UNKNOWN;

    bool valid() const noexcept { return !decisionId.empty(); }
};

class PredictionLedger {
public:
    PredictionLedger() = default;

    // Append a prediction. Predictions are append-only; a duplicate decision
    // identity is rejected rather than overwritten.
    bool append(const PredictionRecord& record);

    bool linkOutcome(const EntityId& decisionId, const Outcome& outcome);

    bool contains(const EntityId& decisionId) const;
    bool get(const EntityId& decisionId, PredictionRecord& out) const;

    const std::vector<PredictionRecord>& records() const noexcept { return records_; }
    std::size_t size() const noexcept { return records_.size(); }

    // Unresolved predictions (no outcome yet).
    std::vector<PredictionRecord> unresolved() const;

private:
    std::vector<PredictionRecord> records_;
};

}  // namespace aura
