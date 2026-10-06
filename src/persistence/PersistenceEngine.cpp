// SHD-0014 - Persistence engine implementation.

#include "persistence/PersistenceEngine.h"

#include <iomanip>
#include <sstream>

namespace aura {

namespace {

// Doubles must survive a write/read round-trip so reconciliation compares
// equal values rather than reporting a spurious mismatch.
std::string exact(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

}  // namespace

PersistenceEngine::PersistenceEngine(IPersistenceStore* store,
                                     PersistenceEngineConfig config)
    : store_(store), config_(std::move(config)), hasher_(makeHasher(HashAlgorithm::SHA256)) {}

PersistenceRecordMetadata PersistenceEngine::metadataFor(
    const std::string& type, const std::string& key) const {
    PersistenceRecordMetadata metadata;
    metadata.recordId = EntityId(key);
    metadata.recordType = type;
    metadata.schemaVersion = Version{1, 0, 0};
    metadata.createdAt = Timestamp::now();
    metadata.updatedAt = metadata.createdAt;
    return metadata;
}

std::string PersistenceEngine::encodePrediction(const PredictionRecord& record) const {
    std::ostringstream out;
    out << "{\"decision_id\":\"" << record.decisionId.value() << "\""
        << ",\"timeframe\":\"" << toString(record.timeframe) << "\""
        << ",\"as_of\":\"" << record.asOfBarOpenSec << "\""
        << ",\"direction\":\"" << toString(record.direction) << "\""
        << ",\"score\":" << record.score
        << ",\"confidence\":" << record.confidence
        << ",\"probability\":" << record.probabilityEstimate
        << ",\"probability_calibrated\":"
        << (record.probabilityCalibrated ? "true" : "false")
        << ",\"reference\":" << record.referencePrice
        << ",\"stop\":" << record.stopPrice
        << ",\"target\":" << record.targetPrice
        << ",\"has_outcome\":" << (record.hasOutcome ? "true" : "false")
        << ",\"realized_r\":" << record.realizedR
        << "}";
    return out.str();
}

std::string PersistenceEngine::encodePosition(const SimulatedPosition& position) const {
    std::ostringstream out;
    // Field order is relied upon by the reconciliation engine.
    out << exact(position.entryPrice) << "|" << exact(position.stopPrice) << "|"
        << exact(position.targetPrice) << "|" << exact(position.lots) << "|"
        << toString(position.state) << "|" << exact(position.exitPrice) << "|"
        << exact(position.realizedPnL);
    return out.str();
}

std::string PersistenceEngine::encodeOutcome(const Outcome& outcome) const {
    std::ostringstream out;
    out << "{\"outcome_id\":\"" << outcome.outcomeId.value() << "\""
        << ",\"position_id\":\"" << outcome.positionId.value() << "\""
        << ",\"decision_id\":\"" << outcome.decisionId.value() << "\""
        << ",\"classification\":\"" << toString(outcome.classification) << "\""
        << ",\"pnl\":" << outcome.realizedPnL
        << ",\"r\":" << outcome.realizedR
        << "}";
    return out.str();
}

bool PersistenceEngine::persistPrediction(const PredictionRecord& record) {
    if (store_ == nullptr || !store_->isAvailable()) return false;
    if (!record.valid()) return false;

    const std::string payload = encodePrediction(record);
    PersistenceRecordMetadata metadata =
        metadataFor(config_.predictionCollection, record.decisionId.value());
    metadata.idempotencyKey = record.decisionId.value();
    if (hasher_) metadata.contentHash = hasher_->hash(payload);

    const PersistenceStatus status = store_->put(
        config_.predictionCollection, record.decisionId.value(), payload, metadata);
    return isPersistenceSuccess(status);
}

bool PersistenceEngine::persistPosition(const SimulatedPosition& position) {
    if (store_ == nullptr || !store_->isAvailable()) return false;
    if (position.positionId.empty()) return false;

    const std::string payload = encodePosition(position);
    PersistenceRecordMetadata metadata =
        metadataFor(config_.positionCollection, position.positionId.value());
    metadata.idempotencyKey = position.positionId.value();
    if (hasher_) metadata.contentHash = hasher_->hash(payload);

    const PersistenceStatus status = store_->put(
        config_.positionCollection, position.positionId.value(), payload, metadata);
    return isPersistenceSuccess(status);
}

bool PersistenceEngine::persistOutcome(const Outcome& outcome) {
    if (store_ == nullptr || !store_->isAvailable()) return false;
    if (!outcome.valid()) return false;

    const std::string payload = encodeOutcome(outcome);
    PersistenceRecordMetadata metadata =
        metadataFor(config_.outcomeCollection, outcome.outcomeId.value());
    metadata.idempotencyKey = outcome.outcomeId.value();
    if (hasher_) metadata.contentHash = hasher_->hash(payload);

    std::uint64_t sequence = 0;
    const PersistenceStatus status = store_->append(
        config_.outcomeCollection, payload, metadata, sequence);
    return isPersistenceSuccess(status);
}

bool PersistenceEngine::persistLedgerSnapshot(const PredictionLedger& ledger) {
    if (store_ == nullptr || !store_->isAvailable()) return false;
    bool ok = true;
    for (const auto& record : ledger.records()) {
        if (!persistPrediction(record)) ok = false;
    }
    return ok;
}

bool PersistenceEngine::isAvailable() const {
    return store_ != nullptr && store_->isAvailable();
}

}  // namespace aura
