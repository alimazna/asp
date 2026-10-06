// DEC-0027 - Portfolio risk implementation.

#include "risk/PortfolioRiskEngine.h"

#include <algorithm>

namespace aura {

PortfolioDecision PortfolioRiskEngine::admit(const PortfolioExposure& exposure,
                                             const RiskProposal& proposal) const {
    PortfolioDecision decision;

    if (proposal.decision == RiskDecision::DENIED) {
        decision.allowed = false;
        decision.reason = "underlying risk proposal denied";
        return decision;
    }
    if (exposure.openPositions >= config_.maxConcurrentPositions) {
        decision.allowed = false;
        decision.reason = "maximum concurrent positions reached";
        return decision;
    }

    const double headroom =
        config_.maxAggregateRiskFraction - exposure.aggregateRiskFraction;
    if (headroom <= 0.0) {
        decision.allowed = false;
        decision.reason = "aggregate risk limit already reached";
        return decision;
    }

    double approved = std::min(proposal.riskFraction, config_.maxSingleRiskFraction);
    approved = std::min(approved, headroom);
    if (approved <= 0.0) {
        decision.allowed = false;
        decision.reason = "no risk headroom";
        return decision;
    }

    decision.allowed = true;
    decision.approvedRiskFraction = approved;
    decision.reason = approved < proposal.riskFraction ? "risk reduced to portfolio headroom"
                                                       : "risk accepted within limits";
    return decision;
}

}  // namespace aura
