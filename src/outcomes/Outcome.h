#pragma once
// SHD-0008 - Outcome record for a closed shadow position.
//
// Outcomes are the factual result of a simulated position. They are the input
// to research, never a claim of profitability.

#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"
#include "shadow/PositionSimulator.h"

#include <string>

namespace aura {

enum class OutcomeClass {
    WIN,
    LOSS,
    BREAKEVEN,
    UNKNOWN,
};

inline const char* toString(OutcomeClass c) noexcept {
    switch (c) {
        case OutcomeClass::WIN:       return "WIN";
        case OutcomeClass::LOSS:      return "LOSS";
        case OutcomeClass::BREAKEVEN: return "BREAKEVEN";
        case OutcomeClass::UNKNOWN:   return "UNKNOWN";
    }
    return "UNKNOWN";
}

struct Outcome {
    EntityId outcomeId;
    EntityId positionId;
    EntityId decisionId;
    EntityId commandId;
    SignalDirection direction = SignalDirection::NONE;
    Timeframe timeframe = Timeframe::M15;
    PositionState exitState = PositionState::OPEN;
    double entryPrice = 0.0;
    double exitPrice = 0.0;
    double lots = 0.0;
    double realizedPnL = 0.0;
    double realizedR = 0.0;
    double riskFraction = 0.0;
    std::int64_t barsHeld = 0;
    OutcomeClass classification = OutcomeClass::UNKNOWN;
    Timestamp recordedAt;
    std::string note;

    bool valid() const noexcept { return !outcomeId.empty() && !positionId.empty(); }
};

}  // namespace aura
