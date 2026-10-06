// VAL-0012 - Holdout service implementation.

#include "validation/HoldoutService.h"

#include <cmath>

namespace aura {

bool HoldoutService::seal(const std::vector<Bar>& allClosedBars) {
    if (sealed_) return false;
    if (allClosedBars.size() < 2) return false;

    const std::size_t split = static_cast<std::size_t>(
        std::floor(static_cast<double>(allClosedBars.size()) *
                   (1.0 - config_.holdoutFraction)));
    if (split == 0 || split >= allClosedBars.size()) return false;

    visible_.assign(allClosedBars.begin(), allClosedBars.begin() + split);
    holdout_.assign(allClosedBars.begin() + split, allClosedBars.end());
    if (holdout_.size() < config_.minHoldoutBars) {
        visible_.clear();
        holdout_.clear();
        return false;
    }
    sealed_ = true;
    return true;
}

ValidationEvidence HoldoutService::releaseAndValidate(const EntityId& candidateId,
                                                      const ReplayEngine& engine) {
    ValidationEvidence evidence;
    evidence.kind = ValidationKind::HOLDOUT;
    evidence.candidateId = candidateId;
    evidence.recordedAt = Timestamp::now();

    if (!sealed_) {
        evidence.detail = "no holdout sealed";
        return evidence;
    }
    if (releases_ >= config_.maxReleases) {
        // A second look at the holdout would invalidate it as a holdout.
        evidence.detail = "holdout already released; further release refused";
        return evidence;
    }
    ++releases_;

    const ReplayReport report = engine.replay(holdout_);
    if (!report.valid) {
        evidence.detail = "holdout replay failed: " + report.error;
        return evidence;
    }

    evidence.samples = report.barsProcessed;
    evidence.sufficientEvidence = report.barsProcessed >= config_.minHoldoutBars;
    const double decisionRate = report.barsProcessed > 0
                                    ? static_cast<double>(report.decisionsProduced) /
                                          static_cast<double>(report.barsProcessed)
                                    : 0.0;
    evidence.metric = decisionRate;
    evidence.passed = evidence.sufficientEvidence && decisionRate > 0.0;
    evidence.detail = "holdout decisionRate=" + std::to_string(decisionRate) +
                      " (single release consumed)";
    return evidence;
}

}  // namespace aura
