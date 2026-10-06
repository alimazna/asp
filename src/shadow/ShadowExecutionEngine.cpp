// SHD-0003 - Shadow execution engine implementation.

#include "shadow/ShadowExecutionEngine.h"

#include <algorithm>

namespace aura {

ShadowExecutionResult ShadowExecutionEngine::issue(
    const RiskProposal& proposal, const PortfolioDecision& portfolio,
    const EntityId& decisionId, Timeframe timeframe, std::int64_t asOfBarOpenSec,
    const std::string& rationale) {
    ShadowExecutionResult result;

    if (!portfolio.allowed) {
        result.reason = "portfolio denied: " + portfolio.reason;
        return result;
    }
    if (proposal.decision == RiskDecision::DENIED || !proposal.valid) {
        result.reason = "risk proposal not approved";
        return result;
    }
    if (decisionId.empty()) {
        result.reason = "missing deterministic decision identity";
        return result;
    }

    // Guardian must permit shadow execution. Live is never requested.
    if (guardian_ != nullptr) {
        GuardianRequest request;
        request.action = "shadow.execute";
        request.actor = "shadow-execution-engine";
        request.affectsExecution = true;
        request.isLiveExecution = false;
        request.correlationId = decisionId;
        const GuardianVerdict verdict = guardian_->evaluate(request);
        if (verdict.decision != GuardianDecision::ALLOW) {
            result.reason = "guardian did not allow shadow execution: " + verdict.reason;
            return result;
        }
    }

    ShadowCommand command;
    command.decisionId = decisionId;
    command.commandId = EntityId("shadow-cmd-" + decisionId.value() + "-" +
                                 std::to_string(static_cast<long long>(asOfBarOpenSec)));
    command.timeframe = timeframe;
    command.asOfBarOpenSec = asOfBarOpenSec;
    command.direction = proposal.direction;
    command.entryPrice = proposal.entryPrice;
    command.stopPrice = proposal.stopPrice;
    command.targetPrice = proposal.targetPrice;
    command.requestedLots = proposal.positionSizeLots;
    command.approvedRiskFraction = portfolio.approvedRiskFraction;
    command.state = ShadowCommandState::ISSUED;
    command.createdAt = Timestamp::now();
    command.rationale = rationale;
    command.isShadowOnly = true;

    if (!command.valid()) {
        result.reason = "constructed shadow command failed validation";
        return result;
    }

    commands_.push_back(command);
    result.accepted = true;
    result.command = command;
    result.reason = "shadow command issued";
    return result;
}

bool ShadowExecutionEngine::cancel(const EntityId& commandId,
                                   const std::string& reason) {
    for (auto& command : commands_) {
        if (command.commandId == commandId) {
            if (command.state == ShadowCommandState::FILLED) return false;
            command.state = ShadowCommandState::CANCELLED;
            command.rationale += " | cancelled: " + reason;
            return true;
        }
    }
    return false;
}

bool ShadowExecutionEngine::markFilled(const EntityId& commandId) {
    for (auto& command : commands_) {
        if (command.commandId == commandId) {
            if (command.state != ShadowCommandState::ISSUED) return false;
            command.state = ShadowCommandState::FILLED;
            return true;
        }
    }
    return false;
}

}  // namespace aura
