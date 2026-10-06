#pragma once
// VAL-0003 - Out-of-sample validator.
//
// Splits closed bars into an in-sample and an out-of-sample partition and
// replays both. A candidate is only considered to have passed when the
// out-of-sample sample is large enough and the descriptive metric does not
// collapse relative to in-sample.

#include "evolution/CandidateRegistry.h"
#include "foundation/EntityId.h"
#include "replay/ReplayEngine.h"
#include "validation/ValidationFirewall.h"

#include <string>

namespace aura {

struct OOSConfig {
    double outOfSampleFraction = 0.3;
    std::size_t minOutOfSampleBars = 50;
    double maxDegradation = 0.5;   // oos/in-sample metric ratio floor
};

class OOSValidator {
public:
    explicit OOSValidator(OOSConfig config = {}) : config_(config) {}

    // Replay the split and produce firewall evidence for the candidate.
    ValidationEvidence validate(const EntityId& candidateId,
                                const std::vector<Bar>& closedBars,
                                const ReplayEngine& engine) const;

    const OOSConfig& config() const noexcept { return config_; }

private:
    OOSConfig config_;
};

}  // namespace aura
