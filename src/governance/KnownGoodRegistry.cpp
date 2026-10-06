// GOV-0006 - Known-good registry implementation.

#include "governance/KnownGoodRegistry.h"

namespace aura {

bool KnownGoodRegistry::record(const EntityId& candidateId,
                               const std::string& label,
                               const HashDigest& configurationHash,
                               const std::string& configurationSnapshot,
                               const EntityId& approvedBy, Timestamp now) {
    if (candidateId.empty()) return false;

    KnownGoodEntry entry;
    entry.entryId = EntityId("known-good-" + std::to_string(++sequence_));
    entry.candidateId = candidateId;
    entry.label = label;
    entry.configurationHash = configurationHash;
    entry.configurationSnapshot = configurationSnapshot;
    entry.approvedBy = approvedBy;
    entry.recordedAt = now.isUnknown() ? Timestamp::now() : now;
    entry.current = false;
    entries_[entry.entryId] = entry;
    return true;
}

bool KnownGoodRegistry::markCurrent(const EntityId& entryId) {
    auto it = entries_.find(entryId);
    if (it == entries_.end()) return false;
    if (!currentId_.empty()) {
        auto previous = entries_.find(currentId_);
        if (previous != entries_.end()) previous->second.current = false;
    }
    it->second.current = true;
    currentId_ = entryId;
    return true;
}

bool KnownGoodRegistry::get(const EntityId& entryId, KnownGoodEntry& out) const {
    auto it = entries_.find(entryId);
    if (it == entries_.end()) return false;
    out = it->second;
    return true;
}

bool KnownGoodRegistry::current(KnownGoodEntry& out) const {
    if (currentId_.empty()) return false;
    return get(currentId_, out);
}

std::vector<KnownGoodEntry> KnownGoodRegistry::all() const {
    std::vector<KnownGoodEntry> result;
    for (const auto& kv : entries_) result.push_back(kv.second);
    return result;
}

}  // namespace aura
