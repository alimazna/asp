#pragma once
// GOV-0006 - Known-good registry.
//
// Records configurations/candidates that have been confirmed working so the
// system can always roll back to a trusted state. Entries are immutable
// snapshots; rollback selects one, it does not modify it.

#include "evolution/CandidateRegistry.h"
#include "foundation/HashDigest.h"
#include "governance/ApprovalGate.h"

#include <map>
#include <string>
#include <vector>

namespace aura {

struct KnownGoodEntry {
    EntityId entryId;
    EntityId candidateId;
    std::string label;
    HashDigest configurationHash;
    std::string configurationSnapshot;
    EntityId approvedBy;             // approval request that authorised it
    Timestamp recordedAt;
    bool current = false;

    bool valid() const noexcept { return !entryId.empty() && !candidateId.empty(); }
};

class KnownGoodRegistry {
public:
    KnownGoodRegistry() = default;

    // Record a new known-good state. Only candidates that passed the promotion
    // gate should be recorded; this class does not re-verify, it records.
    bool record(const EntityId& candidateId, const std::string& label,
                const HashDigest& configurationHash,
                const std::string& configurationSnapshot,
                const EntityId& approvedBy, Timestamp now);

    // Mark an entry as the current known-good state.
    bool markCurrent(const EntityId& entryId);

    bool get(const EntityId& entryId, KnownGoodEntry& out) const;
    bool current(KnownGoodEntry& out) const;
    std::vector<KnownGoodEntry> all() const;

private:
    std::map<EntityId, KnownGoodEntry> entries_;
    EntityId currentId_;
    std::uint64_t sequence_ = 0;
};

}  // namespace aura
