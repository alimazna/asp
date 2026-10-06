#pragma once
// GOV-0004 - Promotion gate.
//
// The only path by which a validated candidate becomes active. Promotion
// requires, in order: a passed validation firewall, a complete change
// investigation report, an approved approval request, and a Guardian that
// permits the action. Any missing precondition blocks promotion.

#include "evolution/CandidateRegistry.h"
#include "governance/ApprovalGate.h"
#include "governance/ChangeInvestigationReport.h"
#include "guardian/IGuardian.h"
#include "validation/ValidationFirewall.h"

#include <string>
#include <vector>

namespace aura {

struct PromotionRequest {
    EntityId candidateId;
    ChangeInvestigationReport investigation;
    std::string requestedBy;
};

struct PromotionVerdict {
    bool promoted = false;
    std::vector<std::string> blockers;
    std::string summary;
};

class PromotionGate {
public:
    PromotionGate(ValidationFirewall* firewall, ApprovalGate* approvals,
                  CandidateRegistry* registry, const IGuardian* guardian)
        : firewall_(firewall), approvals_(approvals), registry_(registry),
          guardian_(guardian) {}

    // Evaluate without mutating anything.
    PromotionVerdict evaluate(const PromotionRequest& request) const;

    // Evaluate and, if admissible, apply the promotion to the registry.
    PromotionVerdict promote(const PromotionRequest& request, Timestamp now);

private:
    ValidationFirewall* firewall_;
    ApprovalGate* approvals_;
    CandidateRegistry* registry_;
    const IGuardian* guardian_;
};

}  // namespace aura
