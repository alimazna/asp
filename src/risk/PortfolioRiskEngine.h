#pragma once
// DEC-0026 - Portfolio-level risk aggregation.
//
// Enforces portfolio exposure limits across concurrent shadow positions. With
// a single instrument (XAUUSD) correlation is trivial, but aggregate risk and
// count limits still apply.

#include "risk/RiskEngine.h"

#include <string>
#include <vector>

namespace aura {

struct PortfolioRiskConfig {
    double maxAggregateRiskFraction = 0.03;   // 3% of equity at risk total
    std::size_t maxConcurrentPositions = 3;
    double maxSingleRiskFraction = 0.01;
};

struct PortfolioExposure {
    std::size_t openPositions = 0;
    double aggregateRiskFraction = 0.0;
    bool limitBreached = false;
    std::vector<std::string> notes;
};

struct PortfolioDecision {
    bool allowed = false;
    double approvedRiskFraction = 0.0;
    std::string reason;
};

class PortfolioRiskEngine {
public:
    explicit PortfolioRiskEngine(PortfolioRiskConfig config = {})
        : config_(config) {}

    PortfolioDecision admit(const PortfolioExposure& exposure,
                            const RiskProposal& proposal) const;

    const PortfolioRiskConfig& config() const noexcept { return config_; }

private:
    PortfolioRiskConfig config_;
};

}  // namespace aura
