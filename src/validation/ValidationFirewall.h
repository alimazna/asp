#pragma once
// VAL-0001 - Validation firewall.
//
// The single gate every candidate must pass before it can be considered
// validated. The firewall is deny-by-default: a candidate is only admitted
// when all mandatory validations have passed on sufficient evidence.

#include "evolution/CandidateComparison.h"
#include "evolution/CandidateRegistry.h"
#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"

#include <string>
#include <vector>

namespace aura {

enum class ValidationKind {
    OUT_OF_SAMPLE,
    WALK_FORWARD,
    STRESS,
    REGRESSION,
    HOLDOUT,
};

inline const char* toString(ValidationKind k) noexcept {
    switch (k) {
        case ValidationKind::OUT_OF_SAMPLE: return "OUT_OF_SAMPLE";
        case ValidationKind::WALK_FORWARD:  return "WALK_FORWARD";
        case ValidationKind::STRESS:        return "STRESS";
        case ValidationKind::REGRESSION:    return "REGRESSION";
        case ValidationKind::HOLDOUT:       return "HOLDOUT";
    }
    return "UNKNOWN";
}

struct ValidationEvidence {
    ValidationKind kind = ValidationKind::OUT_OF_SAMPLE;
    EntityId candidateId;
    bool passed = false;
    bool sufficientEvidence = false;
    std::size_t samples = 0;
    double metric = 0.0;
    std::string detail;
    Timestamp recordedAt;
};

struct FirewallPolicy {
    std::size_t minSamples = 50;
    bool requireOutOfSample = true;
    bool requireWalkForward = true;
    bool requireStress = true;
    bool requireRegression = true;
    bool requireHoldout = true;
};

struct FirewallVerdict {
    bool admitted = false;
    std::vector<ValidationKind> satisfied;
    std::vector<ValidationKind> missing;
    std::vector<std::string> reasons;
    std::string summary;
};

class ValidationFirewall {
public:
    explicit ValidationFirewall(FirewallPolicy policy = {}) : policy_(policy) {}

    void record(const ValidationEvidence& evidence);

    FirewallVerdict evaluate(const EntityId& candidateId) const;

    std::vector<ValidationEvidence> evidenceFor(const EntityId& candidateId) const;

    const FirewallPolicy& policy() const noexcept { return policy_; }

private:
    FirewallPolicy policy_;
    std::vector<ValidationEvidence> evidence_;
};

}  // namespace aura
