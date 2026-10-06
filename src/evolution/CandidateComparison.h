#pragma once
// EVO-0007 - Candidate comparison.
//
// Ranks candidates by descriptive out-of-sample evidence recorded in the
// experiment ledger. Comparison produces a ranking only; it never promotes a
// candidate and never claims profitability.

#include "evolution/CandidateRegistry.h"
#include "research/ExperimentLedger.h"

#include <string>
#include <vector>

namespace aura {

struct CandidateComparisonEntry {
    EntityId candidateId;
    EntityId hypothesisId;
    std::size_t experimentCount = 0;
    std::size_t totalSamples = 0;
    double meanMetric = 0.0;
    double bestMetric = 0.0;
    double worstMetric = 0.0;
    bool sufficientEvidence = false;
    int rank = 0;
};

struct ComparisonReport {
    bool valid = false;
    std::vector<CandidateComparisonEntry> entries;
    std::string caveat;
};

struct ComparisonPolicy {
    std::size_t minSamplesForEvidence = 50;
    bool higherIsBetter = true;
};

class CandidateComparison {
public:
    CandidateComparison(const CandidateRegistry* registry,
                        const ExperimentLedger* experiments,
                        ComparisonPolicy policy = {})
        : registry_(registry), experiments_(experiments), policy_(policy) {}

    ComparisonReport compare() const;

private:
    const CandidateRegistry* registry_;
    const ExperimentLedger* experiments_;
    ComparisonPolicy policy_;
};

}  // namespace aura
