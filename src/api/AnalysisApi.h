#pragma once
// PKG-0009 - Analysis API surface (v1).
//
// The decision-support surface described by
// `docs/frontend/FRONTEND_HANDOFF_GUIDE.md` (T18) and backed by
// `docs/architecture/DECISION_MODEL.md` (T15):
//
//   GET /api/v1/analysis/latest
//   GET /api/v1/analysis/history?limit=N
//   GET /api/v1/context/latest
//   GET /api/v1/health
//
// Honesty rules: every field this surface cannot source is emitted `null` /
// `UNKNOWN`, never fabricated. RULE C binds: a value is shown as a probability
// only through `ProbabilityApi::view`, so the gate is identical to the
// /probability route. Levels come from the live risk proposal (not recomputed);
// `horizon`, `features_contributing`, `confidence_lo/hi`, and `model_version`
// are `null` until the decision model (T15) freezes them.

#include "api/BackendApiSchema.h"
#include "api/ProbabilityApi.h"
#include "health/HealthMonitor.h"
#include "ledger/PredictionLedger.h"
#include "runtime/DecisionPipeline.h"

#include <cstdint>
#include <string>

namespace aura {

class AnalysisApi {
public:
    AnalysisApi() = default;

    // The `data` object of /analysis/latest.
    ApiResponse latest(const std::string& symbol, const DecisionContext& context,
                       const ProbabilityApi* probability,
                       const PredictionLedger* ledger) const;

    // The `data` array of /analysis/history, most-recent-first, capped at
    // `limit`. Per-entry context/levels are not persisted, so they are `null`.
    ApiResponse history(const std::string& symbol, const DecisionContext& context,
                        const ProbabilityApi* probability,
                        const PredictionLedger* ledger, int limit) const;

    // The `data` object of /context/latest.
    ApiResponse contextLatest(const std::string& symbol,
                              const DecisionContext& context) const;

    // The `data` object of /health. `uptimeSec` < 0 means unknown.
    ApiResponse health(const HealthMonitor* health, const std::string& bridgeState,
                       bool bridgeHandshakeOk, std::int64_t uptimeSec) const;
};

}  // namespace aura
