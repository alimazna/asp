// VAL-0002 - Validation firewall implementation.

#include "validation/ValidationFirewall.h"

#include <algorithm>

namespace aura {

void ValidationFirewall::record(const ValidationEvidence& evidence) {
    // Later evidence for the same kind+candidate supersedes earlier evidence.
    for (auto& existing : evidence_) {
        if (existing.candidateId == evidence.candidateId &&
            existing.kind == evidence.kind) {
            existing = evidence;
            return;
        }
    }
    evidence_.push_back(evidence);
}

std::vector<ValidationEvidence> ValidationFirewall::evidenceFor(
    const EntityId& candidateId) const {
    std::vector<ValidationEvidence> result;
    for (const auto& evidence : evidence_) {
        if (evidence.candidateId == candidateId) result.push_back(evidence);
    }
    return result;
}

FirewallVerdict ValidationFirewall::evaluate(const EntityId& candidateId) const {
    FirewallVerdict verdict;

    std::vector<ValidationKind> required;
    if (policy_.requireOutOfSample) required.push_back(ValidationKind::OUT_OF_SAMPLE);
    if (policy_.requireWalkForward) required.push_back(ValidationKind::WALK_FORWARD);
    if (policy_.requireStress) required.push_back(ValidationKind::STRESS);
    if (policy_.requireRegression) required.push_back(ValidationKind::REGRESSION);
    if (policy_.requireHoldout) required.push_back(ValidationKind::HOLDOUT);

    for (ValidationKind kind : required) {
        bool found = false;
        for (const auto& evidence : evidence_) {
            if (evidence.candidateId != candidateId || evidence.kind != kind) continue;
            found = true;
            if (!evidence.passed) {
                verdict.reasons.push_back(std::string(toString(kind)) +
                                          " failed: " + evidence.detail);
            } else if (!evidence.sufficientEvidence ||
                       evidence.samples < policy_.minSamples) {
                verdict.reasons.push_back(
                    std::string(toString(kind)) +
                    " has insufficient evidence (samples=" +
                    std::to_string(evidence.samples) + ")");
            } else {
                verdict.satisfied.push_back(kind);
            }
        }
        if (!found) {
            verdict.missing.push_back(kind);
            verdict.reasons.push_back(std::string(toString(kind)) + " not run");
        }
    }

    verdict.admitted = verdict.missing.empty() && verdict.reasons.empty() &&
                       verdict.satisfied.size() == required.size();
    verdict.summary = verdict.admitted ? "candidate admitted past firewall"
                                       : "candidate blocked by validation firewall";
    return verdict;
}

}  // namespace aura
