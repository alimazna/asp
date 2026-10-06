#pragma once
// RSH-0015 - Experiment ledger.
//
// Append-only record of every research experiment: what was tested, on which
// data, and the observed result. Experiments never mutate live state; the
// ledger is the audit trail of research activity.

#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"
#include "persistence/IPersistenceStore.h"

#include <string>
#include <vector>

namespace aura {

enum class ExperimentOutcome {
    PENDING,
    SUPPORTED,
    REFUTED,
    INCONCLUSIVE,
    ABORTED,
};

inline const char* toString(ExperimentOutcome o) noexcept {
    switch (o) {
        case ExperimentOutcome::PENDING:      return "PENDING";
        case ExperimentOutcome::SUPPORTED:    return "SUPPORTED";
        case ExperimentOutcome::REFUTED:      return "REFUTED";
        case ExperimentOutcome::INCONCLUSIVE: return "INCONCLUSIVE";
        case ExperimentOutcome::ABORTED:      return "ABORTED";
    }
    return "PENDING";
}

struct ExperimentRecord {
    EntityId experimentId;
    EntityId hypothesisId;
    std::string method;
    std::string dataRange;
    std::size_t sampleSize = 0;
    double resultMetric = 0.0;
    ExperimentOutcome outcome = ExperimentOutcome::PENDING;
    std::string notes;
    Timestamp startedAt;
    Timestamp completedAt;

    bool valid() const noexcept { return !experimentId.empty(); }
};

class ExperimentLedger {
public:
    explicit ExperimentLedger(IPersistenceStore* store = nullptr,
                              std::string stream = "experiment_history")
        : store_(store), stream_(std::move(stream)) {}

    EntityId begin(const EntityId& hypothesisId, const std::string& method,
                   const std::string& dataRange, Timestamp startedAt);

    bool complete(const EntityId& experimentId, ExperimentOutcome outcome,
                  double resultMetric, std::size_t sampleSize,
                  const std::string& notes, Timestamp completedAt);

    bool get(const EntityId& experimentId, ExperimentRecord& out) const;
    std::vector<ExperimentRecord> all() const;
    std::vector<ExperimentRecord> forHypothesis(const EntityId& hypothesisId) const;

    std::size_t loadFromStore();

    std::size_t size() const noexcept { return records_.size(); }

private:
    std::string encode(const ExperimentRecord& record) const;
    bool decode(const std::string& payload, ExperimentRecord& out) const;

    IPersistenceStore* store_;
    std::string stream_;
    std::vector<ExperimentRecord> records_;
    std::uint64_t sequence_ = 0;
};

}  // namespace aura
