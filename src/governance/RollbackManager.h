#pragma once
// GOV-0007 - Rollback manager.
//
// Returns the system to a known-good state. Rollback is a governance action:
// it requires an approval and is recorded. It never deletes history and it
// never re-enables a retired candidate.

#include "evolution/CandidateRegistry.h"
#include "governance/ApprovalGate.h"
#include "governance/KnownGoodRegistry.h"
#include "guardian/IGuardian.h"

#include <string>
#include <vector>

namespace aura {

struct RollbackRecord {
    EntityId rollbackId;
    EntityId fromCandidate;
    EntityId toEntry;
    EntityId toCandidate;
    std::string reason;
    std::string performedBy;
    Timestamp performedAt;
    bool succeeded = false;
};

class RollbackManager {
public:
    RollbackManager(KnownGoodRegistry* knownGood, CandidateRegistry* registry,
                    ApprovalGate* approvals, const IGuardian* guardian)
        : knownGood_(knownGood), registry_(registry), approvals_(approvals),
          guardian_(guardian) {}

    // Roll the active candidate back to a known-good entry. Requires that the
    // target entry exists and the Guardian permits the action.
    bool rollback(const EntityId& targetEntryId, const EntityId& fromCandidate,
                  const std::string& reason, const std::string& performedBy,
                  Timestamp now);

    const std::vector<RollbackRecord>& history() const noexcept { return history_; }

private:
    KnownGoodRegistry* knownGood_;
    CandidateRegistry* registry_;
    ApprovalGate* approvals_;
    const IGuardian* guardian_;
    std::vector<RollbackRecord> history_;
    std::uint64_t sequence_ = 0;
};

}  // namespace aura
