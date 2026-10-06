// TST-0007 - Decision -> fill -> position -> outcome lifecycle.

#include "TestHarness.h"

#include "guardian/IGuardian.h"
#include "market_quality/MarketQualityEngine.h"
#include "outcomes/OutcomeEngine.h"
#include "risk/PortfolioRiskEngine.h"
#include "risk/RiskEngine.h"
#include "scoring/ScoreEngine.h"
#include "shadow/PositionSimulator.h"
#include "shadow/ShadowExecutionEngine.h"
#include "signals/SignalEngine.h"

#include <memory>

using namespace aura;

namespace {

constexpr std::int64_t kM15 = 15 * 60;

SignalCandidate longCandidate() {
    SignalCandidate candidate;
    candidate.timeframe = Timeframe::M15;
    candidate.asOfBarOpenSec = 100 * kM15;
    candidate.direction = SignalDirection::LONG;
    candidate.rawStrength = 1.0;
    candidate.referencePrice = 2000.0;
    candidate.suggestedStop = 1995.0;
    candidate.suggestedTarget = 2012.0;
    candidate.quality = DataQualityState::VALID;
    candidate.valid = true;
    return candidate;
}

ScoreResult validScore() {
    ScoreResult score;
    score.score = 75.0;
    score.quality = DataQualityState::VALID;
    score.valid = true;
    return score;
}

MarketQuality validMarketQuality() {
    MarketQuality quality;
    quality.timeframe = Timeframe::M15;
    quality.overall = 0.8;
    quality.quality = DataQualityState::VALID;
    quality.valid = true;
    return quality;
}

}  // namespace

TEST_CASE(full_shadow_lifecycle_produces_outcome) {
    std::unique_ptr<IGuardian> guardian = makeGuardian();

    RiskEngine riskEngine(guardian.get());
    const RiskProposal proposal =
        riskEngine.propose(longCandidate(), validScore(), validMarketQuality());
    REQUIRE(proposal.valid);
    REQUIRE(proposal.decision != RiskDecision::DENIED);

    PortfolioRiskEngine portfolioEngine;
    const PortfolioDecision portfolio =
        portfolioEngine.admit(PortfolioExposure{}, proposal);
    REQUIRE(portfolio.allowed);

    const EntityId decisionId("M15-9000-LONG");
    ShadowExecutionEngine shadowEngine(guardian.get());
    const ShadowExecutionResult issued =
        shadowEngine.issue(proposal, portfolio, decisionId, Timeframe::M15,
                           100 * kM15, "lifecycle test");
    REQUIRE(issued.accepted);
    CHECK(issued.command.isShadowOnly);
    CHECK_EQ(static_cast<int>(issued.command.state),
             static_cast<int>(ShadowCommandState::ISSUED));

    CHECK(shadowEngine.markFilled(issued.command.commandId));

    PositionSimulator simulator;
    CHECK(simulator.open(issued.command, 100 * kM15,
                         Timestamp::fromEpochMillis(100 * kM15 * 1000)));
    CHECK_EQ(simulator.openCount(), static_cast<std::size_t>(1));

    // Advance a later closed bar that reaches the target.
    const Bar targetBar =
        test::makeBar(Timeframe::M15, 101 * kM15, 2000.0,
                      proposal.targetPrice + 1.0, 1999.0,
                      proposal.targetPrice);
    simulator.advance(targetBar);
    CHECK_EQ(simulator.openCount(), static_cast<std::size_t>(0));

    OutcomeEngine outcomeEngine;
    const auto outcomes =
        outcomeEngine.recordClosed(simulator.positions(),
                                   Timestamp::fromEpochMillis(101 * kM15 * 1000));
    REQUIRE(outcomes.size() == 1);
    CHECK_EQ(outcomes[0].decisionId.value(), decisionId.value());
    CHECK(outcomes[0].classification != OutcomeClass::UNKNOWN);
}

TEST_CASE(stop_is_preferred_when_bar_touches_both) {
    std::unique_ptr<IGuardian> guardian = makeGuardian();
    RiskEngine riskEngine(guardian.get());
    const RiskProposal proposal =
        riskEngine.propose(longCandidate(), validScore(), validMarketQuality());
    PortfolioRiskEngine portfolioEngine;
    const PortfolioDecision portfolio =
        portfolioEngine.admit(PortfolioExposure{}, proposal);

    ShadowExecutionEngine shadowEngine(guardian.get());
    const ShadowExecutionResult issued = shadowEngine.issue(
        proposal, portfolio, EntityId("M15-9000-LONG"), Timeframe::M15,
        100 * kM15, "stop precedence test");
    REQUIRE(issued.accepted);

    PositionSimulator simulator;
    REQUIRE(simulator.open(issued.command, 100 * kM15,
                           Timestamp::fromEpochMillis(100 * kM15 * 1000)));

    // A wide bar touches both stop and target: conservative outcome is the stop.
    const Bar bothBar =
        test::makeBar(Timeframe::M15, 101 * kM15, 2000.0,
                      proposal.targetPrice + 5.0, proposal.stopPrice - 5.0, 2000.0);
    simulator.advance(bothBar);

    REQUIRE(simulator.positions().size() == 1);
    CHECK_EQ(static_cast<int>(simulator.positions()[0].state),
             static_cast<int>(PositionState::CLOSED_STOP));
    CHECK(simulator.positions()[0].realizedPnL < 0.0);
}

TEST_CASE(shadow_command_cannot_be_live) {
    std::unique_ptr<IGuardian> guardian = makeGuardian();
    // The Guardian denies any live execution request outright.
    GuardianRequest live;
    live.action = "live.execute";
    live.actor = "test";
    live.affectsExecution = true;
    live.isLiveExecution = true;
    const GuardianVerdict verdict = guardian->evaluate(live);
    CHECK_EQ(static_cast<int>(verdict.decision),
             static_cast<int>(GuardianDecision::DENY));
    CHECK(!guardian->policy().allowLiveExecution);
}

TEST_CASE(halted_guardian_blocks_new_shadow_execution) {
    std::unique_ptr<IGuardian> guardian = makeGuardian();
    CHECK(guardian->halt("test halt"));

    RiskEngine riskEngine(guardian.get());
    const RiskProposal proposal =
        riskEngine.propose(longCandidate(), validScore(), validMarketQuality());
    CHECK_EQ(static_cast<int>(proposal.decision),
             static_cast<int>(RiskDecision::DENIED));
}
