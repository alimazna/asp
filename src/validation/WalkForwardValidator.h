#pragma once
// VAL-0005 - Walk-forward validator.
//
// Replays the bar history in successive rolling windows, each evaluated only
// on data that follows its own training slice. No window may see the future.

#include "foundation/EntityId.h"
#include "replay/ReplayEngine.h"
#include "validation/ValidationFirewall.h"

#include <string>
#include <vector>

namespace aura {

struct WalkForwardConfig {
    std::size_t trainBars = 200;
    std::size_t testBars = 50;
    std::size_t minWindows = 3;
    double maxFailureFraction = 0.34;   // windows allowed to be empty of decisions
};

struct WalkForwardWindow {
    std::size_t trainStart = 0;
    std::size_t trainEnd = 0;
    std::size_t testStart = 0;
    std::size_t testEnd = 0;
    std::size_t decisionsProduced = 0;
};

class WalkForwardValidator {
public:
    explicit WalkForwardValidator(WalkForwardConfig config = {}) : config_(config) {}

    std::vector<WalkForwardWindow> windows(const std::vector<Bar>& closedBars,
                                           const ReplayEngine& engine) const;

    ValidationEvidence validate(const EntityId& candidateId,
                                const std::vector<Bar>& closedBars,
                                const ReplayEngine& engine) const;

    const WalkForwardConfig& config() const noexcept { return config_; }

private:
    WalkForwardConfig config_;
};

}  // namespace aura
