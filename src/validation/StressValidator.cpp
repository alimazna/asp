// VAL-0008 - Stress validator implementation.

#include "validation/StressValidator.h"

#include <algorithm>
#include <cmath>

namespace aura {

std::vector<Bar> StressValidator::stress(const std::vector<Bar>& closedBars) const {
    std::vector<Bar> stressed;
    stressed.reserve(closedBars.size());

    for (std::size_t i = 0; i < closedBars.size(); ++i) {
        Bar bar = closedBars[i];
        bar.spread = static_cast<std::int64_t>(
            std::llround(static_cast<double>(bar.spread) * config_.spreadMultiplier));

        const double mid = (bar.high + bar.low) * 0.5;
        const double halfRange = (bar.high - bar.low) * 0.5 * config_.volatilityMultiplier;
        bar.high = mid + halfRange;
        bar.low = mid - halfRange;

        if (config_.gapStride > 0 && i > 0 && i % config_.gapStride == 0) {
            const double gap = (bar.high - bar.low) * config_.volatilityMultiplier;
            bar.open += gap;
            bar.close += gap;
            bar.high += gap;
            bar.low += gap;
        }

        // Synthetic provenance: quality is never claimed VALID for stressed bars.
        bar.quality = DataQualityState::INCOMPLETE;
        bar.sourceSymbol += "#stressed";
        stressed.push_back(bar);
    }
    return stressed;
}

ValidationEvidence StressValidator::validate(
    const EntityId& candidateId, const std::vector<Bar>& closedBars,
    const ReplayEngine& engine) const {
    ValidationEvidence evidence;
    evidence.kind = ValidationKind::STRESS;
    evidence.candidateId = candidateId;
    evidence.recordedAt = Timestamp::now();

    if (closedBars.size() < config_.minBars) {
        evidence.detail = "insufficient bars for stress test";
        return evidence;
    }

    const std::vector<Bar> stressed = stress(closedBars);
    const ReplayReport report = engine.replay(stressed);
    if (!report.valid) {
        evidence.detail = "stress replay failed: " + report.error;
        return evidence;
    }

    evidence.samples = report.barsProcessed;
    evidence.sufficientEvidence = report.barsProcessed >= config_.minBars;
    const double decisionRate = report.barsProcessed > 0
                                    ? static_cast<double>(report.decisionsProduced) /
                                          static_cast<double>(report.barsProcessed)
                                    : 0.0;
    evidence.metric = decisionRate;
    // A robust candidate still produces decisions under stress (it is not
    // silently disabled by widened spreads / gaps).
    evidence.passed = evidence.sufficientEvidence && decisionRate > 0.0;
    evidence.detail = "stressed decisionRate=" + std::to_string(decisionRate);
    return evidence;
}

}  // namespace aura
