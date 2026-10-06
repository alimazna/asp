#pragma once
// SHD-0013 - Backend persistence engine.
//
// Writes prediction records, outcomes, and simulated positions to the durable
// store. Writes are idempotent at the record boundary and append-only for
// streams. The engine never rewrites history to improve apparent results.

#include "integrity/IHasher.h"
#include "ledger/PredictionLedger.h"
#include "outcomes/OutcomeEngine.h"
#include "persistence/IPersistenceStore.h"
#include "shadow/PositionSimulator.h"

#include <memory>
#include <string>
#include <vector>

namespace aura {

struct PersistenceEngineConfig {
    std::string predictionCollection = "predictions";
    std::string positionCollection = "shadow_positions";
    std::string outcomeCollection = "outcomes";
    std::string predictionStream = "prediction_history";
};

class PersistenceEngine {
public:
    PersistenceEngine(IPersistenceStore* store, PersistenceEngineConfig config = {});

    // Persist a prediction. Idempotent: re-persisting the same decision id is a
    // no-op. Returns true when the store accepted (or already held) the record.
    bool persistPrediction(const PredictionRecord& record);

    // Persist / update a simulated position snapshot (keyed by position id).
    bool persistPosition(const SimulatedPosition& position);

    // Append an outcome to the append-only outcome stream.
    bool persistOutcome(const Outcome& outcome);

    // Persist the full ledger (bounded) for restart recovery.
    bool persistLedgerSnapshot(const PredictionLedger& ledger);

    bool isAvailable() const;

    const PersistenceEngineConfig& config() const noexcept { return config_; }

private:
    PersistenceRecordMetadata metadataFor(const std::string& type,
                                          const std::string& key) const;
    std::string encodePrediction(const PredictionRecord& record) const;
    std::string encodePosition(const SimulatedPosition& position) const;
    std::string encodeOutcome(const Outcome& outcome) const;

    IPersistenceStore* store_;
    PersistenceEngineConfig config_;
    std::unique_ptr<IHasher> hasher_;
};

}  // namespace aura
