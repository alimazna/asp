// VAL-0004 - Out-of-sample validator implementation.

#include "validation/OOSValidator.h"

#include <algorithm>
#include <cmath>

namespace aura {

ValidationEvidence OOSValidator::validate(const EntityId& candidateId,
                                          const std::vector<Bar>& closedBars,
                                          const ReplayEngine& engine) const {
    ValidationEvidence evidence;
    evidence.kind = ValidationKind::OUT_OF_SAMPLE;
    evidence.candidateId = candidateId;
    evidence.recordedAt = Timestamp::now();

    if (closedBars.size() < 2) {
        evidence.detail = "insufficient bars to split";
        return evidence;
    }

    const std::size_t split = static_cast<std::size_t>(
        std::floor(static_cast<double>(closedBars.size()) *
                   (1.0 - config_.outOfSampleFraction)));
    if (split == 0 || split >= closedBars.size()) {
        evidence.detail = "split produced an empty partition";
        return evidence;
    }

    std::vector<Bar> inSample(closedBars.begin(), closedBars.begin() + split);
    std::vector<Bar> outOfSample(closedBars.begin() + split, closedBars.end());

    const ReplayReport inReport = engine.replay(inSample);
    const ReplayReport outReport = engine.replay(outOfSample);
    if (!inReport.valid || !outReport.valid) {
        evidence.detail = "replay failed: " + inReport.error + outReport.error;
        return evidence;
    }

    evidence.samples = outReport.barsProcessed;
    evidence.sufficientEvidence = outReport.barsProcessed >= config_.minOutOfSampleBars;

    const double inRate = inReport.barsProcessed > 0
                              ? static_cast<double>(inReport.decisionsProduced) /
                                    static_cast<double>(inReport.barsProcessed)
                              : 0.0;
    const double outRate = outReport.barsProcessed > 0
                               ? static_cast<double>(outReport.decisionsProduced) /
                                     static_cast<double>(outReport.barsProcessed)
                               : 0.0;
    evidence.metric = outRate;

    // Degradation is measured on decision coverage, a deterministic proxy for
    // behavioural stability. It is not a performance metric.
    const bool degraded = inRate > 0.0 && (outRate / inRate) < config_.maxDegradation;
    evidence.passed = evidence.sufficientEvidence && !degraded;
    evidence.detail = "inRate=" + std::to_string(inRate) +
                      " outRate=" + std::to_string(outRate) +
                      (degraded ? " degraded" : " stable");
    return evidence;
}

}  // namespace aura
