#pragma once
// SHD-0001 - Shadow command: the intent produced by the decision chain.
//
// A shadow command is structurally incapable of live execution. It carries the
// Guardian verdict that authorised it and the deterministic decision identity
// so its lifecycle is reproducible.

#include "foundation/EntityId.h"
#include "foundation/HashDigest.h"
#include "foundation/Timestamp.h"
#include "risk/RiskEngine.h"
#include "signals/SignalEngine.h"

#include <string>

namespace aura {

enum class ShadowCommandState {
    CREATED,
    ISSUED,
    FILLED,
    REJECTED,
    EXPIRED,
    CANCELLED,
};

inline const char* toString(ShadowCommandState s) noexcept {
    switch (s) {
        case ShadowCommandState::CREATED:   return "CREATED";
        case ShadowCommandState::ISSUED:    return "ISSUED";
        case ShadowCommandState::FILLED:    return "FILLED";
        case ShadowCommandState::REJECTED:  return "REJECTED";
        case ShadowCommandState::EXPIRED:   return "EXPIRED";
        case ShadowCommandState::CANCELLED: return "CANCELLED";
    }
    return "UNKNOWN";
}

struct ShadowCommand {
    EntityId commandId;
    EntityId decisionId;            // deterministic decision identity
    Timeframe timeframe = Timeframe::M15;
    std::int64_t asOfBarOpenSec = 0;
    SignalDirection direction = SignalDirection::NONE;
    double entryPrice = 0.0;
    double stopPrice = 0.0;
    double targetPrice = 0.0;
    double requestedLots = 0.0;
    double approvedRiskFraction = 0.0;
    ShadowCommandState state = ShadowCommandState::CREATED;
    Timestamp createdAt;
    std::string rationale;

    // Live execution is impossible by construction in this build.
    bool isShadowOnly = true;

    bool valid() const noexcept {
        return !commandId.empty() && !decisionId.empty() &&
               direction != SignalDirection::NONE && isShadowOnly &&
               entryPrice > 0.0;
    }
};

}  // namespace aura
