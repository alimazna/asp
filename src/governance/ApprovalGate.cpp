// GOV-0002 - Approval gate implementation.

#include "governance/ApprovalGate.h"

namespace aura {

EntityId ApprovalGate::request(ApprovalRequestKind kind, const EntityId& subjectId,
                               const std::string& requestedBy,
                               const std::string& justification, Timestamp now) {
    // Policy: in this build, promotion/config/policy changes always require
    // human approval. The gate never self-approves.
    ApprovalRequest request;
    request.requestId = EntityId("approval-" + std::to_string(++sequence_));
    request.kind = kind;
    request.subjectId = subjectId;
    request.requestedBy = requestedBy;
    request.justification = justification;
    request.status = ApprovalStatus::PENDING;
    request.requestedAt = now.isUnknown() ? Timestamp::now() : now;
    requests_.push_back(request);
    return request.requestId;
}

bool ApprovalGate::approve(const EntityId& requestId, const std::string& approver,
                           const std::string& reason, Timestamp now) {
    for (auto& request : requests_) {
        if (request.requestId != requestId) continue;
        if (request.status != ApprovalStatus::PENDING) return false;
        if (approver.empty()) return false;   // approval must be attributable
        request.status = ApprovalStatus::APPROVED;
        request.decidedBy = approver;
        request.decisionReason = reason;
        request.decidedAt = now.isUnknown() ? Timestamp::now() : now;
        return true;
    }
    return false;
}

bool ApprovalGate::reject(const EntityId& requestId, const std::string& approver,
                          const std::string& reason, Timestamp now) {
    for (auto& request : requests_) {
        if (request.requestId != requestId) continue;
        if (request.status != ApprovalStatus::PENDING) return false;
        request.status = ApprovalStatus::REJECTED;
        request.decidedBy = approver;
        request.decisionReason = reason;
        request.decidedAt = now.isUnknown() ? Timestamp::now() : now;
        return true;
    }
    return false;
}

bool ApprovalGate::withdraw(const EntityId& requestId, const std::string& reason,
                            Timestamp now) {
    for (auto& request : requests_) {
        if (request.requestId != requestId) continue;
        if (request.status != ApprovalStatus::PENDING) return false;
        request.status = ApprovalStatus::WITHDRAWN;
        request.decisionReason = reason;
        request.decidedAt = now.isUnknown() ? Timestamp::now() : now;
        return true;
    }
    return false;
}

bool ApprovalGate::get(const EntityId& requestId, ApprovalRequest& out) const {
    for (const auto& request : requests_) {
        if (request.requestId == requestId) {
            out = request;
            return true;
        }
    }
    return false;
}

bool ApprovalGate::isApproved(const EntityId& subjectId) const {
    for (const auto& request : requests_) {
        if (request.subjectId == subjectId &&
            request.status == ApprovalStatus::APPROVED) {
            return true;
        }
    }
    return false;
}

std::vector<ApprovalRequest> ApprovalGate::all() const { return requests_; }

std::vector<ApprovalRequest> ApprovalGate::pending() const {
    std::vector<ApprovalRequest> result;
    for (const auto& request : requests_) {
        if (request.status == ApprovalStatus::PENDING) result.push_back(request);
    }
    return result;
}

}  // namespace aura
