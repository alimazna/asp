#pragma once
// AYZR-ECO-API — JSON serialization for the ecosystem read surface.
//
// Pure functions: they read engine results, opportunities, the ledger and the
// session snapshot and produce response bodies for the Android app and the
// observatory. No mutation, no secrets, no fabricated fields. Every value that
// is absent is emitted as null or an explicit "غير متاح", never a default.

#include "ayzr/ecosystem/CentralOrchestrator.h"
#include "ayzr/ecosystem/DecisionLedger.h"
#include "ayzr/ecosystem/EngineContract.h"
#include "ayzr/ecosystem/SessionIntelligence.h"

#include <string>
#include <vector>

namespace aura {
namespace ayzr {
namespace eco {

/// Serialize one EngineResult. `includeAr` includes the Arabic labels.
std::string engineResultJson(const EngineResult& r);

/// GET /api/v1/ayzr/ecosystem/sessions — the session snapshot.
std::string sessionsJson(const SessionSnapshot& snap);

/// GET /api/v1/ayzr/ecosystem/engines — the engine registry with health.
struct EngineDescriptor {
    std::string engineId;
    std::string version;
    StrategyFamily family = StrategyFamily::UNKNOWN;
    Timeframe timeframe = Timeframe::M15;
    EngineHealth health = EngineHealth::UNAVAILABLE;
    std::string noteAr;
};
std::string enginesJson(const std::vector<EngineDescriptor>& engines);

/// Serialize one opportunity.
std::string opportunityJson(const Opportunity& op);

/// GET /api/v1/ayzr/ecosystem/opportunities — a cycle result.
std::string opportunitiesJson(const CycleResult& cycle);

/// Serialize one ledger entry.
std::string ledgerEntryJson(const LedgerEntry& e);

/// GET /api/v1/ayzr/ecosystem/ledger — ledger entries plus a count.
std::string ledgerJson(const std::vector<LedgerEntry>& entries);

/// Escape a UTF-8 string for embedding in a JSON string literal.
std::string jsonEscape(const std::string& s);

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
