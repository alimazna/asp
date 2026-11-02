#pragma once
// AYZR-API-001 — JSON serialization for the multi-horizon read surface.
//
// Pure functions: they read engine state and trade records and produce the
// response bodies consumed by the Android app and the AYZR web observatory.
// No mutation, no secrets, no fabricated fields.

#include "api/BackendApiSchema.h"
#include "ayzr/HorizonEngine.h"
#include "ayzr/TradeLifecycle.h"

#include <string>
#include <vector>

namespace aura {
namespace ayzr {

/// GET /api/v1/ayzr/engines — one entry per horizon plus the frozen profile.
std::string enginesJson(const HorizonRegistry& registry);

/// GET /api/v1/ayzr/trades?state=open|closed|all — trade list.
std::string tradesJson(const TradeLifecycleManager& mgr,
                       const std::string& stateFilter);

/// GET /api/v1/ayzr/trades/detail?id=... — a single trade with its events.
/// Returns a 404-shaped body when the id is unknown.
std::string tradeDetailJson(const TradeLifecycleManager& mgr,
                            const std::string& tradeId);

/// Serialize one trade record (used by both list and detail).
std::string tradeRecordJson(const TradeRecord& t, bool includeEvents);

}  // namespace ayzr
}  // namespace aura
