#pragma once
// RES-0036 - Timeframe capability impact.
//
// Maps a timeframe's data quality to the capabilities that are affected when
// that timeframe is not decision-grade, per the V4 isolation rule:
//   - M15 failure blocks M15-triggered decisions;
//   - H4 failure blocks H4-dependent structural authority;
//   - M5/M1 degradation disables microstructure/execution-context features.
// This is a pure derivation from the timeframe role and its quality state, so
// the API can report impact without inventing it.

#include "foundation/DataQualityState.h"
#include "mt5/Mt5BridgeContract.h"

#include <string>
#include <vector>

namespace aura {

struct CapabilityImpact {
    std::string capability;   // human-readable capability name
    std::string impact;       // NONE | DEGRADED | BLOCKED
    std::string reason;
};

inline std::vector<CapabilityImpact> timeframeCapabilityImpact(
    Timeframe timeframe, DataQualityState quality) {
    std::vector<CapabilityImpact> impacts;
    if (isDecisionGrade(quality)) return impacts;   // VALID: nothing affected

    const std::string state = toString(quality);
    const std::string tf = toString(timeframe);
    if (isPrimaryOperational(timeframe)) {
        impacts.push_back({"primary_operational_decisions", "BLOCKED",
                           tf + " not decision-grade (" + state + ")"});
    } else if (isPrimaryStructural(timeframe)) {
        impacts.push_back({"structural_authority", "BLOCKED",
                           tf + " not decision-grade (" + state + ")"});
    } else if (timeframe == Timeframe::M5 || timeframe == Timeframe::M1) {
        impacts.push_back({"microstructure_context", "DEGRADED",
                           tf + " degraded (" + state + ")"});
    } else {
        impacts.push_back({"context_timeframe", "DEGRADED",
                           tf + " degraded (" + state + ")"});
    }
    return impacts;
}

}  // namespace aura
