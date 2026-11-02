#pragma once
// Activity-log and provider-status API response builders.
//
// Pure formatting over already-fetched rows, matching the rest of the API layer:
// no I/O, no decisions. Both surfaces exist so the owner can see, in the app,
// which operations actually succeeded and which AI credential answered.

#include "activity/ActivityLog.h"
#include "api/BackendApiSchema.h"
#include "news/FailoverLlmProvider.h"

#include <string>
#include <vector>

namespace aura {

class ActivityApi {
public:
    // GET /api/v1/activity — newest first, with a per-outcome tally so the app
    // can show a one-line health summary above the list.
    static ApiResponse list(const std::vector<ActivityEntry>& entries,
                            const ActivityLog::Tally& tally);

    // GET /api/v1/providers — per-provider attempt/success tally for the AI
    // failover chain. Lets the owner see which credential is carrying the load
    // and which one is dead, without exposing any key material.
    static ApiResponse providers(const std::vector<FailoverStat>& stats,
                                 const std::string& activeProvider);
};

}  // namespace aura
