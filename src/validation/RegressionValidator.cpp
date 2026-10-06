// VAL-0010 - Regression validator implementation.

#include "validation/RegressionValidator.h"

namespace aura {

std::map<std::string, ContextCoverage> RegressionValidator::coverage(
    const ReplayReport& report) const {
    std::map<std::string, ContextCoverage> result;
    for (const auto& bar : report.results) {
        const std::string key =
            bar.decisionProduced ? toString(bar.direction) : "NONE";
        ContextCoverage& bucket = result[key];
        ++bucket.bars;
        if (bar.decisionProduced) ++bucket.decisions;
    }
    for (auto& kv : result) {
        kv.second.coverage = kv.second.bars > 0
                                 ? static_cast<double>(kv.second.decisions) /
                                       static_cast<double>(kv.second.bars)
                                 : 0.0;
    }
    return result;
}

ValidationEvidence RegressionValidator::validate(const EntityId& candidateId,
                                                 const ReplayReport& reference,
                                                 const ReplayReport& candidate) const {
    ValidationEvidence evidence;
    evidence.kind = ValidationKind::REGRESSION;
    evidence.candidateId = candidateId;
    evidence.recordedAt = Timestamp::now();

    if (!reference.valid || !candidate.valid) {
        evidence.detail = "reference or candidate report invalid";
        return evidence;
    }

    const auto referenceCoverage = coverage(reference);
    const auto candidateCoverage = coverage(candidate);
    evidence.samples = reference.barsProcessed;

    double worstDegradation = 0.0;
    std::string worstContext;
    for (const auto& kv : referenceCoverage) {
        const double before = kv.second.coverage;
        if (before <= 0.0) continue;
        double after = 0.0;
        auto it = candidateCoverage.find(kv.first);
        if (it != candidateCoverage.end()) after = it->second.coverage;
        const double degradation = (before - after) / before;
        if (degradation > worstDegradation) {
            worstDegradation = degradation;
            worstContext = kv.first;
        }
    }

    evidence.metric = worstDegradation;
    evidence.sufficientEvidence = reference.barsProcessed >= config_.minBars;
    evidence.passed = evidence.sufficientEvidence &&
                      worstDegradation <= config_.maxContextDegradation;
    evidence.detail = "worstDegradation=" + std::to_string(worstDegradation) +
                      " context=" + (worstContext.empty() ? "none" : worstContext);
    return evidence;
}

}  // namespace aura
