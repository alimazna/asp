// HOST-0013 - Live decision pipeline implementation.

#include "runtime/DecisionPipeline.h"

namespace aura {

namespace {

std::string makeDecisionId(Timeframe timeframe, std::int64_t barOpenSec,
                           SignalDirection direction) {
    return std::string(toString(timeframe)) + "-" + std::to_string(barOpenSec) +
           "-" + toString(direction);
}

}  // namespace

DecisionPipeline::DecisionPipeline(const IGuardian* guardian,
                                   PredictionLedger* ledger,
                                   PersistenceEngine* persistence,
                                   PositionSimulator* positions,
                                   OutcomeEngine* outcomes,
                                   DecisionPipelineConfig config)
    : config_(config),
      riskEngine_(guardian),
      shadowEngine_(guardian),
      ledger_(ledger),
      persistence_(persistence),
      positions_(positions),
      outcomes_(outcomes) {}

PipelineCycleReport DecisionPipeline::onClosedBar(const Bar& closedBar,
                                                  const std::vector<Bar>& history,
                                                  Timestamp now,
                                                  const MtfContext& mtf) {
    PipelineCycleReport report;

    // Causality: only a genuinely closed bar with a valid open time may be
    // evaluated. A negative open time is invalid; epoch 0 is a valid boundary.
    if (closedBar.openTimeSec < 0) {
        report.issues.push_back("closed bar has invalid open time");
        return report;
    }
    if (history.empty()) {
        report.issues.push_back("no closed-bar history available");
        return report;
    }

    const Timeframe timeframe = config_.operationalTimeframe;
    const std::int64_t asOf = closedBar.openTimeSec;

    // Multi-timeframe core gate. When a cross-timeframe context was built and it
    // shows the operational (M15) or structural-authority (H4) stream missing or
    // not decision-grade, the objective cannot be verified: suppress the
    // decision and report the objective as UNKNOWN. This never fabricates a
    // bias; it is the honest analogue of the research gate
    // `cross.m15Available && cross.h4Available`. When no context is available
    // (single-timeframe caller / tests) the chain behaves as before.
    if (mtf.available && !mtf.coreGatePassed) {
        DecisionContext blocked;
        blocked.available = true;
        blocked.decisionId =
            EntityId(makeDecisionId(timeframe, asOf, SignalDirection::NONE));
        blocked.symbol = symbol_;
        blocked.timeframe = timeframe;
        blocked.asOfBarOpenSec = asOf;
        blocked.direction = SignalDirection::NONE;
        blocked.mtf = mtf;
        blocked.blockReasons.push_back(
            mtf.blockReason.empty() ? "multi-timeframe core gate not passed"
                                    : mtf.blockReason);
        blocked.strategyVersion = strategyVersion_;
        blocked.configurationVersion = configurationVersion_;
        blocked.evaluatedAt = now;
        lastContext_ = blocked;
        report.issues.push_back(blocked.blockReasons.front());
        return report;
    }

    // Macro/event boundary. A high-impact imminent event suppresses the
    // decision. An absent feed is UNKNOWN (not "clear") and is carried through
    // as an explicit uncertainty rather than fabricated as safe.
    const MacroContext macro = macro_.evaluate(now);
    if (macro.highImpactImminent) {
        DecisionContext blocked;
        blocked.available = true;
        blocked.decisionId =
            EntityId(makeDecisionId(timeframe, asOf, SignalDirection::NONE));
        blocked.symbol = symbol_;
        blocked.timeframe = timeframe;
        blocked.asOfBarOpenSec = asOf;
        blocked.direction = SignalDirection::NONE;
        blocked.mtf = mtf;
        blocked.blockReasons.push_back("macro gate: high-impact event window");
        blocked.strategyVersion = strategyVersion_;
        blocked.configurationVersion = configurationVersion_;
        blocked.evaluatedAt = now;
        lastContext_ = blocked;
        report.issues.push_back("macro gate: high-impact event window");
        return report;
    }
    const std::string macroNote =
        macro.valid ? std::string("macro=") + macro.detail : "macro=UNKNOWN";

    const FeatureSnapshot features = featureEngine_.compute(history, timeframe);
    const StructureSnapshot structure =
        structureEngine_.compute(history, features);
    const RegimeState regime = regimeEngine_.compute(features, structure);
    const EligibilityState eligibility =
        eligibilityEngine_.evaluate(features, regime, structure);
    const SignalCandidate candidate =
        signalEngine_.generate(features, structure, regime, eligibility);
    const ScoreResult score =
        scoreEngine_.compute(features, structure, regime, candidate);
    const ConfidenceResult confidence = confidenceEngine_.compute(
        score, structure, regime, candidate, candidate.quality);
    const ProbabilityResult probability =
        probabilityEngine_.compute(score, confidence, candidate);

    // Multi-timeframe veto. When the structural-authority stream (H4) is
    // available and its verified bias directly opposes the operational
    // candidate, the objective contradicts the setup: suppress the decision
    // rather than act against a real higher-timeframe bias. Only fires when the
    // cross-timeframe objective was actually computed (mtf.available), so the
    // single-timeframe caller is unchanged. It never invents a bias: an
    // UNKNOWN H4 leaves the candidate untouched.
    if (mtf.available && mtf.h4Available &&
        candidate.direction != SignalDirection::NONE) {
        const bool candidateLong = candidate.direction == SignalDirection::LONG;
        const bool h4Opposes =
            (candidateLong && mtf.h4Bias == StructureBias::BEARISH) ||
            (!candidateLong && mtf.h4Bias == StructureBias::BULLISH);
        if (h4Opposes && mtf.h4M15Agreement < 0.0) {
            DecisionContext vetoed;
            vetoed.available = true;
            vetoed.decisionId = EntityId(
                makeDecisionId(timeframe, asOf, SignalDirection::NONE));
            vetoed.symbol = symbol_;
            vetoed.timeframe = timeframe;
            vetoed.asOfBarOpenSec = asOf;
            vetoed.direction = SignalDirection::NONE;
            vetoed.dataState = features.quality;
            vetoed.structure = structure.bias;
            vetoed.regime = regime.regime;
            vetoed.eligibility = eligibility.decision;
            vetoed.strategyVersion = strategyVersion_;
            vetoed.configurationVersion = configurationVersion_;
            vetoed.evaluatedAt = now;
            vetoed.mtf = mtf;
            vetoed.blockReasons.push_back(
                "multi-timeframe veto: operational candidate opposes H4 "
                "structural authority");
            lastContext_ = vetoed;
            report.issues.push_back(vetoed.blockReasons.front());
            return report;
        }
    }

    if (candidate.direction == SignalDirection::NONE || !score.valid ||
        !confidence.valid) {
        report.issues.push_back("no decision-grade candidate on this bar");
        // Persist the context anyway: /context/latest must report a real bar
        // timestamp after the first closed bar, not epoch 0. No decision or
        // signal is fabricated — direction stays NONE and available reflects
        // that the chain produced nothing decision-grade this bar.
        DecisionContext noDecision;
        noDecision.available = true;
        noDecision.decisionId =
            EntityId(makeDecisionId(timeframe, asOf, SignalDirection::NONE));
        noDecision.symbol = symbol_;
        noDecision.timeframe = timeframe;
        noDecision.asOfBarOpenSec = asOf;
        noDecision.direction = SignalDirection::NONE;
        noDecision.dataState = features.quality;
        noDecision.structure = structure.bias;
        noDecision.regime = regime.regime;
        noDecision.eligibility = eligibility.decision;
        noDecision.strategyVersion = strategyVersion_;
        noDecision.configurationVersion = configurationVersion_;
        noDecision.evaluatedAt = now;
        noDecision.mtf = mtf;
        // Distinguish the scarcity cause instead of one generic message, so a
        // consumer can tell "data is blocked" from "the market state was
        // rejected" from "nothing directional this bar". These are honest,
        // ordered, and non-exclusive.
        if (features.quality == DataQualityState::UNKNOWN ||
            features.quality == DataQualityState::MISSING ||
            features.quality == DataQualityState::INCOMPLETE) {
            noDecision.blockReasons.push_back("data blocked: feature window not decision-grade");
        }
        if (eligibility.decision == EligibilityDecision::NOT_ELIGIBLE) {
            noDecision.blockReasons.push_back("eligibility rejected the market state");
        }
        if (candidate.direction == SignalDirection::NONE) {
            noDecision.blockReasons.push_back("no directional signal on this bar");
        } else if (!score.valid && !confidence.valid) {
            noDecision.blockReasons.push_back("score and confidence below decision-grade");
        } else if (!score.valid) {
            noDecision.blockReasons.push_back("score below decision-grade threshold");
        } else {
            noDecision.blockReasons.push_back("confidence below decision-grade threshold");
        }
        // When a cross-timeframe objective was computed, expose whether it
        // contradicts the operational direction instead of leaving the caller
        // to infer agreement from nulls.
        if (mtf.available && mtf.h4Available) {
            noDecision.blockReasons.push_back("no verified multi-timeframe objective");
        }
        lastContext_ = noDecision;
        return report;
    }

    report.decisionProduced = true;

    PipelineDecision decision;
    decision.timeframe = timeframe;
    decision.asOfBarOpenSec = asOf;
    decision.direction = candidate.direction;
    decision.score = score.score;
    decision.confidence = confidence.confidence;
    decision.decisionId =
        EntityId(makeDecisionId(timeframe, asOf, candidate.direction));

    // Capture the actual decision inputs for the frontend contract. These are
    // the values the chain just computed, not re-derived ones.
    DecisionContext context;
    context.available = true;
    context.decisionId = decision.decisionId;
    context.symbol = symbol_;
    context.timeframe = timeframe;
    context.asOfBarOpenSec = asOf;
    context.direction = candidate.direction;
    context.dataState = features.quality;
    context.structure = structure.bias;
    context.regime = regime.regime;
    context.eligibility = eligibility.decision;
    context.strategyVersion = strategyVersion_;
    context.configurationVersion = configurationVersion_;
    context.evaluatedAt = now;
    context.mtf = mtf;
    lastContext_ = context;

    // A decision identity is deterministic; a repeat is a duplicate, never a
    // second prediction.
    if (ledger_ != nullptr && ledger_->contains(decision.decisionId)) {
        decision.reason = "duplicate decision identity; ignored";
        report.decisions.push_back(decision);
        return report;
    }

    const std::string rationale = candidate.rationale + " | " + macroNote;

    PredictionRecord record;
    record.decisionId = decision.decisionId;
    record.timeframe = timeframe;
    record.asOfBarOpenSec = asOf;
    record.direction = candidate.direction;
    record.score = score.score;
    record.confidence = confidence.confidence;
    record.probabilityEstimate = probability.valid ? probability.band.estimate : 0.0;
    record.probabilityCalibrated = false;   // never calibrated in this build
    record.referencePrice = candidate.referencePrice;
    record.stopPrice = candidate.suggestedStop;
    record.targetPrice = candidate.suggestedTarget;
    record.rationale = rationale;
    record.recordedAt = now;

    if (ledger_ != nullptr) {
        decision.recorded = ledger_->append(record);
    }
    if (persistence_ != nullptr) {
        persistence_->persistPrediction(record);
    }

    const MarketQuality marketQuality =
        marketQualityEngine_.evaluate(history, features, features.quality);
    const RiskProposal proposal =
        riskEngine_.propose(candidate, score, marketQuality);

    // The risk proposal is computed (not persisted), so carry it on the
    // decision context for the API to report truthfully.
    lastContext_.risk = proposal;
    lastContext_.riskAvailable = true;

    if (proposal.decision == RiskDecision::DENIED || !proposal.valid) {
        decision.reason = "risk denied: " + proposal.reason;
        lastContext_.blockReasons.push_back(decision.reason);
        report.decisions.push_back(decision);
        return report;
    }

    if (positions_ != nullptr) {
        PortfolioExposure exposure;
        exposure.openPositions = positions_->openCount();
        exposure.aggregateRiskFraction = positions_->aggregateOpenRiskFraction();
        const PortfolioDecision portfolio = portfolioEngine_.admit(exposure, proposal);
        if (!portfolio.allowed) {
            decision.reason = "portfolio denied: " + portfolio.reason;
            lastContext_.blockReasons.push_back(decision.reason);
            report.decisions.push_back(decision);
            return report;
        }

        const ShadowExecutionResult shadow = shadowEngine_.issue(
            proposal, portfolio, decision.decisionId, timeframe, asOf, rationale);
        if (shadow.accepted) {
            decision.shadowIssued = true;
            report.shadowIssued = true;
            positions_->open(shadow.command, asOf, now);
            if (persistence_ != nullptr) {
                const auto& opened = positions_->positions().back();
                persistence_->persistPosition(opened);
            }
        } else {
            decision.reason = "shadow not issued: " + shadow.reason;
        }
    }

    report.decisions.push_back(decision);
    return report;
}

void DecisionPipeline::advancePositions(const Bar& closedBar, Timestamp now,
                                        std::vector<std::string>& issues,
                                        std::vector<EntityId>* outcomesOut) {
    if (positions_ == nullptr) return;
    positions_->advance(closedBar);

    if (outcomes_ == nullptr) return;
    const std::vector<Outcome> newly =
        outcomes_->recordClosed(positions_->positions(), now);
    for (const auto& outcome : newly) {
        if (persistence_ != nullptr && !persistence_->persistOutcome(outcome)) {
            issues.push_back("outcome persistence failed for " +
                             outcome.outcomeId.value());
        }
        if (ledger_ != nullptr) {
            ledger_->linkOutcome(outcome.decisionId, outcome);
        }
        if (outcomesOut != nullptr) {
            outcomesOut->push_back(outcome.outcomeId);
        }
    }
}

}  // namespace aura
