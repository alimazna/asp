// SHD-0010 - Outcome engine implementation.

#include "outcomes/OutcomeEngine.h"

#include <cmath>

namespace aura {

bool OutcomeEngine::hasRecorded(const EntityId& positionId) const {
    for (const auto& outcome : outcomes_) {
        if (outcome.positionId == positionId) return true;
    }
    return false;
}

std::vector<Outcome> OutcomeEngine::recordClosed(
    const std::vector<SimulatedPosition>& positions, Timestamp now) {
    std::vector<Outcome> newly;

    for (const auto& position : positions) {
        if (position.isOpen()) continue;
        if (hasRecorded(position.positionId)) continue;

        Outcome outcome;
        outcome.outcomeId = EntityId("outcome-" + position.positionId.value());
        outcome.positionId = position.positionId;
        outcome.decisionId = position.decisionId;
        outcome.commandId = position.commandId;
        outcome.direction = position.direction;
        outcome.timeframe = position.timeframe;
        outcome.exitState = position.state;
        outcome.entryPrice = position.entryPrice;
        outcome.exitPrice = position.exitPrice;
        outcome.lots = position.lots;
        outcome.realizedPnL = position.realizedPnL;
        outcome.realizedR = position.realizedR;
        outcome.riskFraction = position.riskFraction;
        outcome.recordedAt = now;
        outcome.note = position.closeReason;

        const double epsilon = 1e-9;
        if (position.realizedPnL > epsilon) {
            outcome.classification = OutcomeClass::WIN;
        } else if (position.realizedPnL < -epsilon) {
            outcome.classification = OutcomeClass::LOSS;
        } else {
            outcome.classification = OutcomeClass::BREAKEVEN;
        }

        outcomes_.push_back(outcome);
        newly.push_back(outcome);
    }
    return newly;
}

OutcomeStatistics OutcomeEngine::statistics() const {
    OutcomeStatistics stats;
    for (const auto& outcome : outcomes_) {
        ++stats.total;
        switch (outcome.classification) {
            case OutcomeClass::WIN:       ++stats.wins; break;
            case OutcomeClass::LOSS:      ++stats.losses; break;
            case OutcomeClass::BREAKEVEN: ++stats.breakeven; break;
            case OutcomeClass::UNKNOWN:   break;
        }
        stats.totalPnL += outcome.realizedPnL;
        stats.totalR += outcome.realizedR;
    }
    if (stats.total > 0) {
        stats.averageR = stats.totalR / static_cast<double>(stats.total);
        const std::size_t decided = stats.wins + stats.losses;
        stats.winRate = decided > 0 ? static_cast<double>(stats.wins) /
                                          static_cast<double>(decided)
                                    : 0.0;
        stats.valid = true;
    }
    return stats;
}

}  // namespace aura
