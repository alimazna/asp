#pragma once
// VAL-0007 - Stress validator.
//
// Replays synthetic stressed bar series derived from the real history:
// widened spreads, gapped bars, and volatility spikes. The stressed inputs are
// labelled as synthetic and are never presented as real market data.

#include "foundation/EntityId.h"
#include "replay/ReplayEngine.h"
#include "validation/ValidationFirewall.h"

#include <string>
#include <vector>

namespace aura {

struct StressConfig {
    double spreadMultiplier = 3.0;
    double volatilityMultiplier = 2.5;
    double gapProbability = 0.05;   // deterministic stride-based injection
    std::size_t gapStride = 20;
    std::size_t minBars = 50;
};

class StressValidator {
public:
    explicit StressValidator(StressConfig config = {}) : config_(config) {}

    // Build a deterministic stressed copy of the input bars. Every bar in the
    // result is marked INCOMPLETE quality to signal synthetic provenance.
    std::vector<Bar> stress(const std::vector<Bar>& closedBars) const;

    ValidationEvidence validate(const EntityId& candidateId,
                                const std::vector<Bar>& closedBars,
                                const ReplayEngine& engine) const;

    const StressConfig& config() const noexcept { return config_; }

private:
    StressConfig config_;
};

}  // namespace aura
