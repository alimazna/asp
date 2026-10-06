// GOV-0005 - Promotion gate implementation.

#include "governance/PromotionGate.h"

namespace aura {

PromotionVerdict PromotionGate::evaluate(const PromotionRequest& request) const {
    PromotionVerdict verdict;

    if (firewall_ == nullptr || approvals_ == nullptr || registry_ == nullptr) {
        verdict.blockers.push_back("governance dependencies unavailable");
        verdict.summary = "promotion blocked";
        return verdict;
    }

    RegisteredCandidate candidate;
    if (!registry_->get(request.candidateId, candidate)) {
        verdict.blockers.push_back("candidate not registered");
    }

    const FirewallVerdict firewallVerdict =
        firewall_->evaluate(request.candidateId);
    if (!firewallVerdict.admitted) {
        verdict.blockers.push_back("validation firewall not satisfied: " +
                                   firewallVerdict.summary);
        for (const auto& reason : firewallVerdict.reasons) {
            verdict.blockers.push_back("  - " + reason);
        }
    }

    if (!request.investigation.complete) {
        verdict.blockers.push_back("change investigation report incomplete");
    }
    if (request.investigation.candidateId != request.candidateId) {
        verdict.blockers.push_back("investigation report targets a different candidate");
    }

    if (!approvals_->isApproved(request.candidateId)) {
        verdict.blockers.push_back("no approved approval request for candidate");
    }

    if (guardian_ != nullptr) {
        GuardianRequest guardianRequest;
        guardianRequest.action = "governance.promote";
        guardianRequest.actor = request.requestedBy;
        guardianRequest.affectsExecution = false;   // promotion is not execution
        guardianRequest.isLiveExecution = false;
        guardianRequest.correlationId = request.candidateId;
        const GuardianVerdict guardianVerdict = guardian_->evaluate(guardianRequest);
        if (guardianVerdict.decision != GuardianDecision::ALLOW) {
            verdict.blockers.push_back("guardian denied promotion: " +
                                       guardianVerdict.reason);
        }
    }

    verdict.promoted = verdict.blockers.empty();
    verdict.summary = verdict.promoted ? "promotion admissible"
                                       : "promotion blocked by preconditions";
    return verdict;
}

PromotionVerdict PromotionGate::promote(const PromotionRequest& request,
                                        Timestamp now) {
    PromotionVerdict verdict = evaluate(request);
    if (!verdict.promoted) return verdict;

    if (!registry_->setStatus(request.candidateId, CandidateStatus::PROMOTED,
                              "promoted through governance gate", now)) {
        verdict.promoted = false;
        verdict.blockers.push_back("registry refused promotion");
        verdict.summary = "promotion blocked at registry";
    }
    return verdict;
}

}  // namespace aura
