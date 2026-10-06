#pragma once
// SHD-0002 - Shadow execution engine.
//
// Turns an approved risk proposal into a shadow command and records its
// lifecycle. It never calls a broker. It cannot produce a live order.

#include "guardian/IGuardian.h"
#include "risk/PortfolioRiskEngine.h"
#include "shadow/ShadowCommand.h"

#include <string>
#include <vector>

namespace aura {

struct ShadowExecutionResult {
    bool accepted = false;
    ShadowCommand command;
    std::string reason;
};

class ShadowExecutionEngine {
public:
    explicit ShadowExecutionEngine(const IGuardian* guardian) : guardian_(guardian) {}

    // Create and issue a shadow command from an approved risk proposal.
    // `decisionId` is the deterministic identity of the decision that produced
    // the proposal; it is required.
    ShadowExecutionResult issue(const RiskProposal& proposal,
                                const PortfolioDecision& portfolio,
                                const EntityId& decisionId,
                                Timeframe timeframe,
                                std::int64_t asOfBarOpenSec,
                                const std::string& rationale);

    bool cancel(const EntityId& commandId, const std::string& reason);
    bool markFilled(const EntityId& commandId);

    const std::vector<ShadowCommand>& commands() const noexcept { return commands_; }

private:
    const IGuardian* guardian_;
    std::vector<ShadowCommand> commands_;
};

}  // namespace aura
