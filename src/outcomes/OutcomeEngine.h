#pragma once
// SHD-0009 - Outcome engine.
//
// Derives outcome records from closed positions and keeps aggregate statistics.
// Aggregates are descriptive only and carry no profitability claim.

#include "outcomes/Outcome.h"
#include "shadow/PositionSimulator.h"

#include <string>
#include <vector>

namespace aura {

struct OutcomeStatistics {
    std::size_t total = 0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    std::size_t breakeven = 0;
    double totalPnL = 0.0;
    double totalR = 0.0;
    double averageR = 0.0;
    double winRate = 0.0;   // descriptive, not predictive
    bool valid = false;
};

class OutcomeEngine {
public:
    OutcomeEngine() = default;

    // Record outcomes for any newly-closed positions not yet recorded.
    std::vector<Outcome> recordClosed(const std::vector<SimulatedPosition>& positions,
                                      Timestamp now);

    const std::vector<Outcome>& outcomes() const noexcept { return outcomes_; }

    OutcomeStatistics statistics() const;

    bool hasRecorded(const EntityId& positionId) const;

private:
    std::vector<Outcome> outcomes_;
};

}  // namespace aura
