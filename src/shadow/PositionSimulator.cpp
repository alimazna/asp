// SHD-0005 - Position simulator implementation.

#include "shadow/PositionSimulator.h"

#include <cmath>

namespace aura {

bool PositionSimulator::open(const ShadowCommand& command,
                             std::int64_t asOfBarOpenSec, Timestamp openedAt) {
    if (!command.valid()) return false;

    SimulatedPosition position;
    position.positionId = EntityId("pos-" + command.commandId.value());
    position.commandId = command.commandId;
    position.decisionId = command.decisionId;
    position.direction = command.direction;
    position.entryPrice = command.entryPrice;
    position.stopPrice = command.stopPrice;
    position.targetPrice = command.targetPrice;
    position.lots = command.requestedLots;
    position.riskFraction = command.approvedRiskFraction;
    position.contractSize = contractSize_;
    position.timeframe = command.timeframe;
    position.state = PositionState::OPEN;
    position.openedAt = openedAt;
    position.openedBarOpenSec = asOfBarOpenSec;
    positions_.push_back(position);
    return true;
}

double PositionSimulator::computePnL(const SimulatedPosition& position,
                                     double exitPrice) const {
    const double direction = position.direction == SignalDirection::LONG ? 1.0 : -1.0;
    const double priceDelta = (exitPrice - position.entryPrice) * direction;
    return priceDelta * position.lots * position.contractSize;
}

void PositionSimulator::advance(const Bar& closedBar) {
    for (auto& position : positions_) {
        if (!position.isOpen()) continue;
        if (closedBar.timeframe != position.timeframe) continue;
        if (closedBar.openTimeSec <= position.openedBarOpenSec) continue;

        const bool isLong = position.direction == SignalDirection::LONG;
        const bool stopHit = isLong ? closedBar.low <= position.stopPrice
                                    : closedBar.high >= position.stopPrice;
        const bool targetHit = isLong ? closedBar.high >= position.targetPrice
                                      : closedBar.low <= position.targetPrice;

        // Conservative: stop takes precedence when both are touched.
        if (stopHit) {
            position.state = PositionState::CLOSED_STOP;
            position.exitPrice = position.stopPrice;
            position.closeReason = "stop hit";
        } else if (targetHit) {
            position.state = PositionState::CLOSED_TARGET;
            position.exitPrice = position.targetPrice;
            position.closeReason = "target hit";
        } else {
            continue;
        }

        position.closedAt = Timestamp::fromEpochMillis(closedBar.closeTimeSec() * 1000);
        position.closedBarOpenSec = closedBar.openTimeSec;
        position.realizedPnL = computePnL(position, position.exitPrice);
        const double riskPerUnit =
            std::fabs(position.entryPrice - position.stopPrice) * position.lots *
            position.contractSize;
        position.realizedR = riskPerUnit > 0.0 ? position.realizedPnL / riskPerUnit : 0.0;
    }
}

bool PositionSimulator::closeManually(const EntityId& positionId, double exitPrice,
                                      Timestamp closedAt, const std::string& reason) {
    for (auto& position : positions_) {
        if (position.positionId != positionId) continue;
        if (!position.isOpen()) return false;
        position.state = PositionState::CLOSED_MANUAL;
        position.exitPrice = exitPrice;
        position.closedAt = closedAt;
        position.closeReason = reason;
        position.realizedPnL = computePnL(position, exitPrice);
        const double riskPerUnit =
            std::fabs(position.entryPrice - position.stopPrice) * position.lots *
            position.contractSize;
        position.realizedR =
            riskPerUnit > 0.0 ? position.realizedPnL / riskPerUnit : 0.0;
        return true;
    }
    return false;
}

std::vector<SimulatedPosition> PositionSimulator::openPositions() const {
    std::vector<SimulatedPosition> open;
    for (const auto& position : positions_) {
        if (position.isOpen()) open.push_back(position);
    }
    return open;
}

std::size_t PositionSimulator::openCount() const {
    std::size_t count = 0;
    for (const auto& position : positions_) {
        if (position.isOpen()) ++count;
    }
    return count;
}

double PositionSimulator::realizedPnLTotal() const {
    double total = 0.0;
    for (const auto& position : positions_) {
        if (!position.isOpen()) total += position.realizedPnL;
    }
    return total;
}

double PositionSimulator::aggregateOpenRiskFraction() const {
    double total = 0.0;
    for (const auto& position : positions_) {
        if (position.isOpen()) total += position.riskFraction;
    }
    return total;
}

}  // namespace aura
