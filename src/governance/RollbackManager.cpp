// GOV-0008 - Rollback manager implementation.

#include "governance/RollbackManager.h"

namespace aura {

bool RollbackManager::rollback(const EntityId& targetEntryId,
                               const EntityId& fromCandidate,
                               const std::string& reason,
                               const std::string& performedBy, Timestamp now) {
    RollbackRecord record;
    record.rollbackId = EntityId("rollback-" + std::to_string(++sequence_));
    record.fromCandidate = fromCandidate;
    record.toEntry = targetEntryId;
    record.reason = reason;
    record.performedBy = performedBy;
    record.performedAt = now.isUnknown() ? Timestamp::now() : now;

    if (knownGood_ == nullptr) {
        history_.push_back(record);
        return false;
    }

    KnownGoodEntry target;
    if (!knownGood_->get(targetEntryId, target)) {
        history_.push_back(record);
        return false;
    }
    record.toCandidate = target.candidateId;

    if (guardian_ != nullptr) {
        GuardianRequest request;
        request.action = "governance.rollback";
        request.actor = performedBy;
        request.affectsExecution = true;
        request.isLiveExecution = false;
        request.correlationId = targetEntryId;
        const GuardianVerdict verdict = guardian_->evaluate(request);
        if (verdict.decision != GuardianDecision::ALLOW) {
            history_.push_back(record);
            return false;
        }
    }

    if (registry_ != nullptr) {
        // Retire the currently active candidate, if any.
        if (!fromCandidate.empty()) {
            registry_->setStatus(fromCandidate, CandidateStatus::RETIRED,
                                 "rolled back: " + reason, record.performedAt);
        }
    }

    record.succeeded = knownGood_->markCurrent(targetEntryId);
    history_.push_back(record);
    return record.succeeded;
}

}  // namespace aura
