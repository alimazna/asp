#pragma once
// VAL-0009 - Regression validator.
//
// Guards against a candidate that improves one context while silently
// degrading another. Every context bucket present in the reference run must
// remain within a bounded tolerance in the candidate run.

#include "foundation/EntityId.h"
#include "replay/ReplayEngine.h"
#include "validation/ValidationFirewall.h"

#include <map>
#include <string>
#include <vector>

namespace aura {

struct RegressionConfig {
    double maxContextDegradation = 0.5;   // allowed drop in decision coverage
    std::size_t minBars = 50;
};

struct ContextCoverage {
    std::size_t bars = 0;
    std::size_t decisions = 0;
    double coverage = 0.0;
};

class RegressionValidator {
public:
    explicit RegressionValidator(RegressionConfig config = {}) : config_(config) {}

    // Build decision coverage per timeframe/direction bucket from a replay.
    std::map<std::string, ContextCoverage> coverage(const ReplayReport& report) const;

    // Reference vs candidate comparison. Both reports must be produced by the
    // same replay engine over comparable inputs.
    ValidationEvidence validate(const EntityId& candidateId,
                                const ReplayReport& reference,
                                const ReplayReport& candidate) const;

    const RegressionConfig& config() const noexcept { return config_; }

private:
    RegressionConfig config_;
};

}  // namespace aura
