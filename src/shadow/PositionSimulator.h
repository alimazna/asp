#pragma once
// SHD-0004 - Simulated position lifecycle.
//
// Simulates fills and P&L deterministically from closed bars. No broker is
// contacted. Fills are evaluated against bar high/low with explicit,
// conservative ordering (stop before target within the same bar).

#include "data/BarNormalizer.h"
#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"
#include "shadow/ShadowCommand.h"

#include <string>
#include <vector>

namespace aura {

enum class PositionState {
    OPEN,
    CLOSED_TARGET,
    CLOSED_STOP,
    CLOSED_MANUAL,
    CLOSED_EXPIRED,
};

inline const char* toString(PositionState s) noexcept {
    switch (s) {
        case PositionState::OPEN:           return "OPEN";
        case PositionState::CLOSED_TARGET:  return "CLOSED_TARGET";
        case PositionState::CLOSED_STOP:    return "CLOSED_STOP";
        case PositionState::CLOSED_MANUAL:  return "CLOSED_MANUAL";
        case PositionState::CLOSED_EXPIRED: return "CLOSED_EXPIRED";
    }
    return "UNKNOWN";
}

struct SimulatedPosition {
    EntityId positionId;
    EntityId commandId;
    EntityId decisionId;
    SignalDirection direction = SignalDirection::NONE;
    double entryPrice = 0.0;
    double stopPrice = 0.0;
    double targetPrice = 0.0;
    double lots = 0.0;
    double riskFraction = 0.0;
    double contractSize = 100.0;
    Timeframe timeframe = Timeframe::M15;
    PositionState state = PositionState::OPEN;
    Timestamp openedAt;
    Timestamp closedAt;
    std::int64_t openedBarOpenSec = 0;
    std::int64_t closedBarOpenSec = 0;
    double exitPrice = 0.0;
    double realizedPnL = 0.0;      // account currency
    double realizedR = 0.0;        // in units of risk
    std::string closeReason;

    bool isOpen() const noexcept { return state == PositionState::OPEN; }
};

class PositionSimulator {
public:
    explicit PositionSimulator(double contractSize = 100.0)
        : contractSize_(contractSize) {}

    // Open a simulated position from a filled shadow command.
    bool open(const ShadowCommand& command, std::int64_t asOfBarOpenSec,
              Timestamp openedAt);

    // Advance all open positions against a newly-closed bar. Conservative:
    // if a bar touches both stop and target, the stop is assumed hit first.
    void advance(const Bar& closedBar);

    bool closeManually(const EntityId& positionId, double exitPrice,
                       Timestamp closedAt, const std::string& reason);

    const std::vector<SimulatedPosition>& positions() const noexcept {
        return positions_;
    }
    std::vector<SimulatedPosition> openPositions() const;
    std::size_t openCount() const;

    double realizedPnLTotal() const;
    double aggregateOpenRiskFraction() const;

private:
    double computePnL(const SimulatedPosition& position, double exitPrice) const;

    double contractSize_;
    std::vector<SimulatedPosition> positions_;
};

}  // namespace aura
