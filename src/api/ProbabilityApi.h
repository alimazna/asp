#pragma once
// PKG-0008 - Probability API surface (v1).
//
// RULE C (binding): a value may be presented as a *probability* only when it
// has been calibrated AND the calibration has been independently audited. Until
// then the surface must return `calibrated=false` and must not expose the raw
// number as `probability`.
//
// This module mirrors the producer contract in `src/models/api_contract.py`
// (DRAFT, for later handoff to Agent-C). It computes nothing and sources every
// field from the prediction ledger; it never fabricates a value.
//
// Wire shape of `data`:
//
//   {
//     "available": true|false,
//     "reason": "...",                 // only when available=false
//     "calibrated": true|false,
//     "probability": 0.0-1.0 | null,   // null unless calibrated
//     "score": <number> | null,        // always the uncalibrated score
//     "direction": "UP"|"DOWN"|"NONE",
//     "timestamp": "YYYY-MM-DDTHH:MM:SSZ",
//     "confidence_interval": [lo,hi] | null,
//     "coverage_tier": "high"|"medium"|"low" | null,
//     "model_version": "v1.0" | null,
//     "decision_id": "...",
//     "trigger_timeframe": "M15",
//     "as_of_bar_open_sec": <int>
//   }

#include "api/BackendApiSchema.h"
#include "ledger/PredictionLedger.h"

#include <string>

namespace aura {

// Coverage tiers mirror `TIER_BOUNDS` in `src/models/calibration.py`:
//   low    [0, 1/3)
//   medium [1/3, 2/3)
//   high   [2/3, 1]
// `boundariesMatchProducer` returns true when the C++ boundaries agree with the
// producer contract, so drift between the two is detectable rather than silent.
bool probabilityTierBoundariesMatchProducer() noexcept;

// The coverage tier a probability belongs to. Returns nullptr when the value is
// outside [0,1] (never guesses a tier for an invalid number).
const char* probabilityTier(double probability) noexcept;

// The audited, calibrated-probability surface. Holds only the audit status; the
// number itself comes from the ledger.
class ProbabilityApi {
public:
    ProbabilityApi() = default;

    // The calibration audit is the gate (RULE C). Default false: no audit has
    // blessed a calibrated output, so nothing may be presented as a probability.
    void setCalibrationAudited(bool audited) noexcept { audited_ = audited; }
    bool calibrationAudited() const noexcept { return audited_; }

    // Render the latest prediction as a probability payload. `ledger` may be
    // null (503) or empty (available=false). When the value is not calibrated,
    // `probability` is null and `calibrated` is false — the score is carried in
    // `score`, never disguised as a probability.
    ApiResponse latest(const PredictionLedger* ledger) const;

private:
    bool audited_ = false;
};

}  // namespace aura
