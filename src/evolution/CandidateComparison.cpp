// EVO-0008 - Candidate comparison implementation.

#include "evolution/CandidateComparison.h"

#include <algorithm>
#include <limits>

namespace aura {

ComparisonReport CandidateComparison::compare() const {
    ComparisonReport report;
    report.caveat = "descriptive ranking only; not a profitability claim";
    if (registry_ == nullptr || experiments_ == nullptr) return report;

    for (const auto& registered : registry_->all()) {
        if (registered.candidate.originHypothesis.empty()) continue;

        CandidateComparisonEntry entry;
        entry.candidateId = registered.candidate.candidateId;
        entry.hypothesisId = registered.candidate.originHypothesis;
        entry.bestMetric = -std::numeric_limits<double>::infinity();
        entry.worstMetric = std::numeric_limits<double>::infinity();

        // Match experiments that reference this candidate by identity in notes.
        const auto experiments =
            experiments_->forHypothesis(registered.candidate.originHypothesis);
        double sum = 0.0;
        for (const auto& experiment : experiments) {
            if (experiment.outcome != ExperimentOutcome::SUPPORTED &&
                experiment.outcome != ExperimentOutcome::REFUTED) {
                continue;
            }
            if (experiment.notes.find(registered.candidate.candidateId.value()) ==
                std::string::npos) {
                continue;
            }
            ++entry.experimentCount;
            entry.totalSamples += experiment.sampleSize;
            sum += experiment.resultMetric;
            entry.bestMetric = std::max(entry.bestMetric, experiment.resultMetric);
            entry.worstMetric = std::min(entry.worstMetric, experiment.resultMetric);
        }
        if (entry.experimentCount > 0) {
            entry.meanMetric = sum / static_cast<double>(entry.experimentCount);
        } else {
            entry.bestMetric = 0.0;
            entry.worstMetric = 0.0;
        }
        entry.sufficientEvidence =
            entry.totalSamples >= policy_.minSamplesForEvidence;
        report.entries.push_back(entry);
    }

    std::sort(report.entries.begin(), report.entries.end(),
              [this](const CandidateComparisonEntry& a,
                     const CandidateComparisonEntry& b) {
                  if (a.sufficientEvidence != b.sufficientEvidence) {
                      return a.sufficientEvidence;
                  }
                  if (a.meanMetric != b.meanMetric) {
                      return policy_.higherIsBetter ? a.meanMetric > b.meanMetric
                                                    : a.meanMetric < b.meanMetric;
                  }
                  return a.candidateId.value() < b.candidateId.value();
              });
    for (std::size_t i = 0; i < report.entries.size(); ++i) {
        report.entries[i].rank = static_cast<int>(i) + 1;
    }
    report.valid = true;
    return report;
}

}  // namespace aura
