#pragma once
// HOST-0012 - Deterministic live decision pipeline.
//
// The runtime composition root runs the decision chain once per closed bar of
// the primary operational timeframe (M15). This is the live analogue of the
// replay engine: it consumes only closed bars already published on the data
// bus, records the decision BEFORE any shadow command is issued, and never
// touches a broker.
//
// C++ remains the decision core. The Python bridge only supplies data.

#include "confidence/ConfidenceEngine.h"
#include "eligibility/EligibilityEngine.h"
#include "features/FeatureEngine.h"
#include "guardian/IGuardian.h"
#include "ledger/PredictionLedger.h"
#include "macro/MacroContextEngine.h"
#include "market_quality/MarketQualityEngine.h"
#include "outcomes/OutcomeEngine.h"
#include "persistence/PersistenceEngine.h"
#include "probability/ProbabilityEngine.h"
#include "regime/RegimeEngine.h"
#include "risk/PortfolioRiskEngine.h"
#include "risk/RiskEngine.h"
#include "scoring/ScoreEngine.h"
#include "shadow/PositionSimulator.h"
#include "shadow/ShadowExecutionEngine.h"
#include "signals/SignalEngine.h"
#include "structure/StructureEngine.h"

#include <string>
#include <vector>

namespace aura {

// Identity of the implemented decision-chain strategy. This is a truthful
// identifier for the code that produced the decision, not a market-data value.
inline constexpr const char* kStrategyVersion = "decision-chain-1";

struct DecisionPipelineConfig {
    Timeframe operationalTimeframe = Timeframe::M15;
    std::size_t historyBars = 200;   // closed bars visible to the chain

    // Deterministic identity of this configuration, so a decision can be tied
    // to the exact config that produced it.
    std::string versionString() const {
        return std::string("op=") + toString(operationalTimeframe) + ",hist=" +
               std::to_string(historyBars);
    }
};

// One evaluated decision, whether or not a shadow command followed.
struct PipelineDecision {
    EntityId decisionId;
    Timeframe timeframe = Timeframe::M15;
    std::int64_t asOfBarOpenSec = 0;
    SignalDirection direction = SignalDirection::NONE;
    double score = 0.0;
    double confidence = 0.0;
    bool shadowIssued = false;
    bool recorded = false;
    std::string reason;
};

struct PipelineCycleReport {
    bool decisionProduced = false;
    bool shadowIssued = false;
    std::vector<std::string> issues;
    std::vector<PipelineDecision> decisions;
    // Outcome identities recorded during position advancement this cycle.
    std::vector<EntityId> outcomesRecorded;
};

// Per-decision metadata that the frontend contract exposes but which is not
// stored in the prediction ledger (structure/regime/eligibility and the risk
// proposal are computed, not persisted). Captured for the most recent
// evaluated decision so the API can report the actual decision inputs without
// recomputing or fabricating them.
struct DecisionContext {
    bool available = false;
    EntityId decisionId;
    std::string symbol;
    Timeframe timeframe = Timeframe::M15;
    std::int64_t asOfBarOpenSec = 0;
    SignalDirection direction = SignalDirection::NONE;
    DataQualityState dataState = DataQualityState::UNKNOWN;
    StructureBias structure = StructureBias::UNKNOWN;
    RegimeType regime = RegimeType::UNKNOWN;
    EligibilityDecision eligibility = EligibilityDecision::UNKNOWN;
    RiskProposal risk;             // valid only when riskAvailable
    bool riskAvailable = false;
    std::string strategyVersion;
    std::string configurationVersion;
    Timestamp evaluatedAt;
};

class DecisionPipeline {
public:
    DecisionPipeline(const IGuardian* guardian, PredictionLedger* ledger,
                     PersistenceEngine* persistence, PositionSimulator* positions,
                     OutcomeEngine* outcomes,
                     DecisionPipelineConfig config = {});

    // Evaluate one closed bar. `history` is the closed-bar window ending at
    // `closedBar` (ascending). Returns what was produced; never throws.
    PipelineCycleReport onClosedBar(const Bar& closedBar,
                                    const std::vector<Bar>& history,
                                    Timestamp now);

    // Advance open positions against a newly closed bar and persist any
    // outcomes that became known. Non-critical: a failure here is reported,
    // not fatal.
    void advancePositions(const Bar& closedBar, Timestamp now,
                          std::vector<std::string>& issues,
                          std::vector<EntityId>* outcomesOut = nullptr);

    MacroContextEngine& macro() noexcept { return macro_; }
    const DecisionPipelineConfig& config() const noexcept { return config_; }

    // Metadata for the most recently evaluated decision (available == false
    // until a decision is produced). Read-only view for the API layer.
    const DecisionContext& lastDecisionContext() const noexcept {
        return lastContext_;
    }

    void setSymbol(std::string symbol) { symbol_ = std::move(symbol); }

private:
    DecisionPipelineConfig config_;
    FeatureEngine featureEngine_;
    StructureEngine structureEngine_;
    RegimeEngine regimeEngine_;
    EligibilityEngine eligibilityEngine_;
    SignalEngine signalEngine_;
    ScoreEngine scoreEngine_;
    ConfidenceEngine confidenceEngine_;
    ProbabilityEngine probabilityEngine_;
    MacroContextEngine macro_;
    MarketQualityEngine marketQualityEngine_;
    RiskEngine riskEngine_;
    PortfolioRiskEngine portfolioEngine_;
    ShadowExecutionEngine shadowEngine_;

    PredictionLedger* ledger_;
    PersistenceEngine* persistence_;
    PositionSimulator* positions_;
    OutcomeEngine* outcomes_;

    DecisionContext lastContext_;
    std::string symbol_;
    std::string strategyVersion_ = kStrategyVersion;
    std::string configurationVersion_ = config_.versionString();
};

}  // namespace aura
