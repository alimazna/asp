// RSH-0016 - Experiment ledger implementation.

#include "research/ExperimentLedger.h"

#include <sstream>

namespace aura {

namespace {

std::string escape(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '|' || c == '\\' || c == '\n') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '|') {
            parts.push_back(current);
            current.clear();
        } else if (text[i] == '\\' && i + 1 < text.size()) {
            current.push_back(text[++i]);
        } else {
            current.push_back(text[i]);
        }
    }
    parts.push_back(current);
    return parts;
}

ExperimentOutcome parseOutcome(const std::string& text) {
    for (ExperimentOutcome candidate :
         {ExperimentOutcome::PENDING, ExperimentOutcome::SUPPORTED,
          ExperimentOutcome::REFUTED, ExperimentOutcome::INCONCLUSIVE,
          ExperimentOutcome::ABORTED}) {
        if (text == toString(candidate)) return candidate;
    }
    return ExperimentOutcome::PENDING;
}

}  // namespace

std::string ExperimentLedger::encode(const ExperimentRecord& record) const {
    std::ostringstream out;
    out << escape(record.experimentId.value()) << "|"
        << escape(record.hypothesisId.value()) << "|" << escape(record.method) << "|"
        << escape(record.dataRange) << "|" << record.sampleSize << "|"
        << record.resultMetric << "|" << toString(record.outcome) << "|"
        << escape(record.notes) << "|" << record.startedAt.epochMillis() << "|"
        << record.completedAt.epochMillis();
    return out.str();
}

bool ExperimentLedger::decode(const std::string& payload,
                              ExperimentRecord& out) const {
    const auto parts = split(payload);
    if (parts.size() < 10) return false;
    out.experimentId = EntityId(parts[0]);
    out.hypothesisId = EntityId(parts[1]);
    out.method = parts[2];
    out.dataRange = parts[3];
    try {
        out.sampleSize = static_cast<std::size_t>(std::stoull(parts[4]));
        out.resultMetric = std::stod(parts[5]);
        out.startedAt = Timestamp::fromEpochMillis(std::stoll(parts[8]));
        out.completedAt = Timestamp::fromEpochMillis(std::stoll(parts[9]));
    } catch (...) {
        return false;
    }
    out.outcome = parseOutcome(parts[6]);
    out.notes = parts[7];
    return out.valid();
}

EntityId ExperimentLedger::begin(const EntityId& hypothesisId,
                                 const std::string& method,
                                 const std::string& dataRange,
                                 Timestamp startedAt) {
    ExperimentRecord record;
    record.experimentId = EntityId("experiment-" + std::to_string(++sequence_));
    record.hypothesisId = hypothesisId;
    record.method = method;
    record.dataRange = dataRange;
    record.outcome = ExperimentOutcome::PENDING;
    record.startedAt = startedAt.isUnknown() ? Timestamp::now() : startedAt;
    records_.push_back(record);
    return record.experimentId;
}

bool ExperimentLedger::complete(const EntityId& experimentId,
                                ExperimentOutcome outcome, double resultMetric,
                                std::size_t sampleSize, const std::string& notes,
                                Timestamp completedAt) {
    for (auto& record : records_) {
        if (record.experimentId != experimentId) continue;
        if (record.outcome != ExperimentOutcome::PENDING) return false;
        record.outcome = outcome;
        record.resultMetric = resultMetric;
        record.sampleSize = sampleSize;
        record.notes = notes;
        record.completedAt = completedAt.isUnknown() ? Timestamp::now() : completedAt;

        if (store_ != nullptr && store_->isAvailable()) {
            const std::string payload = encode(record);
            PersistenceRecordMetadata metadata;
            metadata.recordId = record.experimentId;
            metadata.recordType = stream_;
            metadata.schemaVersion = Version{1, 0, 0};
            metadata.createdAt = record.startedAt;
            metadata.updatedAt = record.completedAt;
            metadata.idempotencyKey = record.experimentId.value();
            std::uint64_t sequence = 0;
            store_->append(stream_, payload, metadata, sequence);
        }
        return true;
    }
    return false;
}

bool ExperimentLedger::get(const EntityId& experimentId,
                           ExperimentRecord& out) const {
    for (const auto& record : records_) {
        if (record.experimentId == experimentId) {
            out = record;
            return true;
        }
    }
    return false;
}

std::vector<ExperimentRecord> ExperimentLedger::all() const { return records_; }

std::vector<ExperimentRecord> ExperimentLedger::forHypothesis(
    const EntityId& hypothesisId) const {
    std::vector<ExperimentRecord> result;
    for (const auto& record : records_) {
        if (record.hypothesisId == hypothesisId) result.push_back(record);
    }
    return result;
}

std::size_t ExperimentLedger::loadFromStore() {
    if (store_ == nullptr || !store_->isAvailable()) return 0;
    std::size_t loaded = 0;
    for (const auto& payload : store_->readStream(stream_)) {
        ExperimentRecord record;
        if (decode(payload, record)) {
            records_.push_back(record);
            ++loaded;
        }
    }
    return loaded;
}

}  // namespace aura
