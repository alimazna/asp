#pragma once
// VAL-0011 - Holdout service.
//
// Reserves a sealed partition of bar history that is never visible to
// candidate generation, optimisation, or ordinary validation. It is released
// at most once, for a final confirmatory check, and the release is recorded.
// This is the structural guard against lookahead/overfitting.

#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"
#include "replay/ReplayEngine.h"
#include "validation/ValidationFirewall.h"

#include <string>
#include <vector>

namespace aura {

struct HoldoutConfig {
    double holdoutFraction = 0.2;
    std::size_t minHoldoutBars = 50;
    std::size_t maxReleases = 1;
};

class HoldoutService {
public:
    explicit HoldoutService(HoldoutConfig config = {}) : config_(config) {}

    // Seal the tail of the supplied history as the holdout set. Returns false
    // if a holdout is already sealed or the data is too small.
    bool seal(const std::vector<Bar>& allClosedBars);

    bool isSealed() const noexcept { return sealed_; }
    std::size_t sealedBars() const noexcept { return holdout_.size(); }
    std::size_t releases() const noexcept { return releases_; }

    // History that may be used freely (everything before the seal).
    std::vector<Bar> visibleHistory() const { return visible_; }

    // Release the holdout exactly once and validate against it.
    ValidationEvidence releaseAndValidate(const EntityId& candidateId,
                                          const ReplayEngine& engine);

private:
    HoldoutConfig config_;
    bool sealed_ = false;
    std::vector<Bar> visible_;
    std::vector<Bar> holdout_;
    std::size_t releases_ = 0;
};

}  // namespace aura
