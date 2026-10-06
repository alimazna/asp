#pragma once
// GOV-0001 - Approval gate.
//
// The single place where a human approval is required and recorded. In this
// build the gate can only authorise promotion of a validated candidate; it can
// never authorise live execution. Approval requests and decisions are
// append-only and attributable.

#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"
#include "guardian/IGuardian.h"

#include <string>
#include <vector>

namespace aura {

enum class ApprovalRequestKind {
    CANDIDATE_PROMOTION,
    CONFIG_CHANGE,
    POLICY_CHANGE,
    RESEARCH_MUTATION,
};

inline const char* toString(ApprovalRequestKind k) noexcept {
    switch (k) {
        case ApprovalRequestKind::CANDIDATE_PROMOTION: return "CANDIDATE_PROMOTION";
        case ApprovalRequestKind::CONFIG_CHANGE:       return "CONFIG_CHANGE";
        case ApprovalRequestKind::POLICY_CHANGE:       return "POLICY_CHANGE";
        case ApprovalRequestKind::RESEARCH_MUTATION:   return "RESEARCH_MUTATION";
    }
    return "UNKNOWN";
}

enum class ApprovalStatus {
    PENDING,
    APPROVED,
    REJECTED,
    EXPIRED,
    WITHDRAWN,
};

inline const char* toString(ApprovalStatus s) noexcept {
    switch (s) {
        case ApprovalStatus::PENDING:   return "PENDING";
        case ApprovalStatus::APPROVED:  return "APPROVED";
        case ApprovalStatus::REJECTED:  return "REJECTED";
        case ApprovalStatus::EXPIRED:   return "EXPIRED";
        case ApprovalStatus::WITHDRAWN: return "WITHDRAWN";
    }
    return "UNKNOWN";
}

struct ApprovalRequest {
    EntityId requestId;
    ApprovalRequestKind kind = ApprovalRequestKind::CANDIDATE_PROMOTION;
    EntityId subjectId;             // e.g. candidate id
    std::string requestedBy;
    std::string justification;
    ApprovalStatus status = ApprovalStatus::PENDING;
    Timestamp requestedAt;
    Timestamp decidedAt;
    std::string decidedBy;
    std::string decisionReason;

    bool valid() const noexcept { return !requestId.empty(); }
};

class ApprovalGate {
public:
    explicit ApprovalGate(const IGuardian* guardian = nullptr)
        : guardian_(guardian) {}

    // Open an approval request. Returns an empty id on refusal.
    EntityId request(ApprovalRequestKind kind, const EntityId& subjectId,
                     const std::string& requestedBy,
                     const std::string& justification, Timestamp now);

    bool approve(const EntityId& requestId, const std::string& approver,
                 const std::string& reason, Timestamp now);

    bool reject(const EntityId& requestId, const std::string& approver,
                const std::string& reason, Timestamp now);

    bool withdraw(const EntityId& requestId, const std::string& reason,
                  Timestamp now);

    bool get(const EntityId& requestId, ApprovalRequest& out) const;
    bool isApproved(const EntityId& subjectId) const;
    std::vector<ApprovalRequest> all() const;
    std::vector<ApprovalRequest> pending() const;

private:
    const IGuardian* guardian_;
    std::vector<ApprovalRequest> requests_;
    std::uint64_t sequence_ = 0;
};

}  // namespace aura
