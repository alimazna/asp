#pragma once
// AUD-0004 - Append-only audit log.
//
// Holds the hash-chained audit records that the persistence layer already
// defines the contract for. Records are only ever appended: a re-append of the
// same event identity is rejected rather than rewriting history. This is the
// authoritative source the frontend audit view reads, so incidents are never
// relabelled as audit records.

#include "audit/AuditRecord.h"
#include "integrity/IHasher.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace aura {

class AuditLog {
public:
    AuditLog();

    // Append a record. Fills the sequence, hash-chain link, and record digest
    // from the current tail. A duplicate event identity is rejected.
    bool append(AuditRecord& record);

    bool contains(const EntityId& eventId) const;

    // Most recent records, newest first, bounded by `limit` (0 means all).
    std::vector<AuditRecord> recent(std::size_t limit) const;

    const std::vector<AuditRecord>& records() const noexcept { return records_; }
    std::size_t size() const noexcept { return records_.size(); }

private:
    std::vector<AuditRecord> records_;
    std::unique_ptr<IHasher> hasher_;
};

}  // namespace aura
