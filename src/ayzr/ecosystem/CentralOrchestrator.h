#pragma once
// AYZR-ORC-001 — Central Orchestrator + Portfolio Intelligence.
//
// The orchestrator consumes EngineResult values from every engine and produces
// a single, explained decision per opportunity. It NEVER recomputes engine math,
// never votes by simple majority, and never mutates a frozen ASTRA output.
//
// Pipeline:
//   1. Opportunity identity   — group events that describe the same setup.
//   2. Deduplication          — identical event ids collapse to one.
//   3. Conflict detection     — opposing directions on the same setup.
//   4. Evidence evaluation    — weighted, quality-gated, reason-coded.
//   5. Decision               — PUBLISH / PUBLISH_WITH_CAUTION / WATCH / WITHHOLD.
//   6. Ledger                 — every step is appended, with reason codes.

#include "ayzr/ecosystem/DecisionLedger.h"
#include "ayzr/ecosystem/EngineContract.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aura {
namespace ayzr {
namespace eco {

/// Final decision states the spec defines.
enum class DecisionState {
    PUBLISH,
    PUBLISH_WITH_CAUTION,
    WATCH,
    WITHHOLD,
};

const char* toString(DecisionState s) noexcept;
const char* decisionStateLabelAr(DecisionState s) noexcept;

/// An opportunity groups the events that describe one market setup. Its id is
/// deterministic over (symbol, timeframe, direction, anchor bar) so the same
/// setup keeps the same identity across cycles and a restart does not fork it.
struct Opportunity {
    std::string opportunityId;
    std::string symbol = "XAUUSD";
    Timeframe timeframe = Timeframe::M15;
    EngineDirection direction = EngineDirection::NO_TRADE;
    std::int64_t anchorBarUtc = 0;
    std::int64_t createdAt = 0;
    std::int64_t updatedAt = 0;
    std::optional<std::int64_t> expiresAt;
    DecisionState state = DecisionState::WITHHOLD;
    std::vector<EngineResult> contributingEvents;
    std::vector<std::string> agreeingEngines;
    std::vector<std::string> disagreeingEngines;
    std::vector<std::string> conflictNotesAr;
    std::string rationaleAr;
    /// The single best level set chosen from the agreeing evidence, with the
    /// engine it came from named so provenance is never lost.
    std::optional<PriceLevel> entry;
    std::optional<PriceLevel> stopLoss;
    std::vector<PriceLevel> takeProfitLevels;
    std::string levelSourceEngine;
    bool astraCorePresent = false;   // true when a frozen ASTRA event contributed
    bool hasConflict = false;
    std::vector<std::string> riskFlags;
};

/// Per-engine weight used by the evidence evaluation. Weights express the
/// ecosystem's *declared* trust in a family; they are not probabilities.
struct EngineWeight {
    StrategyFamily family = StrategyFamily::UNKNOWN;
    double weight = 1.0;
};

struct OrchestratorConfig {
    std::vector<EngineWeight> weights = {
        {StrategyFamily::ASTRA_CORE, 1.5},
        {StrategyFamily::TREND_FOLLOWING, 1.2},
        {StrategyFamily::BREAKOUT, 1.1},
        {StrategyFamily::MEAN_REVERSION, 1.0},
        {StrategyFamily::EVENT_DRIVEN, 1.0},
        {StrategyFamily::HORIZON_SHORT, 0.9},
        {StrategyFamily::HORIZON_MEDIUM, 0.9},
        {StrategyFamily::HORIZON_LONG, 0.9},
        {StrategyFamily::MICROSTRUCTURE, 0.5},
    };
    /// Below this weighted agreement score => WATCH; below `withholdBelow` =>
    /// WITHHOLD. These are transparent thresholds, not probabilities.
    double watchAbove = 0.0;
    double publishAbove = 1.0;
    /// A conflict downgrades PUBLISH to PUBLISH_WITH_CAUTION.
    bool downgradeOnConflict = true;
    /// When true, a non-VALID data quality forces WITHHOLD regardless of score.
    bool requireDecisionGradeData = true;
};

/// The orchestrator's output for one cycle.
struct CycleResult {
    std::int64_t asOfUtc = 0;
    std::string correlationId;
    std::vector<Opportunity> opportunities;
    std::vector<std::string> engineFailures;   // engineId: reason
    std::vector<std::string> dataQualityWarnings;
};

/// Weighted, explainable agreement score for one direction.
struct AgreementScore {
    double score = 0.0;                 // sum of weights of agreeing engines
    double opposingScore = 0.0;         // sum of weights of opposing engines
    std::vector<std::string> agreeing;
    std::vector<std::string> opposing;
};

class CentralOrchestrator {
public:
    CentralOrchestrator(DecisionLedger* ledger, OrchestratorConfig cfg = {})
        : ledger_(ledger), cfg_(cfg) {}

    /// Run one decision cycle over all engine outputs for a correlation id.
    /// `nowUtc` is the cycle instant. Results are grouped, deduplicated,
    /// conflict-checked, scored, decided, and appended to the ledger.
    CycleResult runCycle(const std::vector<EngineResult>& events,
                         std::int64_t nowUtc,
                         const std::string& correlationId);

    /// Deterministic opportunity id for a setup.
    static std::string makeOpportunityId(const std::string& symbol, Timeframe tf,
                                         EngineDirection dir,
                                         std::int64_t anchorBarUtc);

private:
    double weightFor(StrategyFamily f) const;
    AgreementScore scoreDirection(const std::vector<EngineResult>& group,
                                  EngineDirection dir) const;

    DecisionLedger* ledger_;
    OrchestratorConfig cfg_;
};

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
