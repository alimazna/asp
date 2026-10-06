#pragma once
// AUD-0003 - Audit contract: an append-only audit record.
//
// Records form a hash chain: each record carries the digest of the previous
// record. This makes silent history rewriting detectable. Records are never
// mutated after append.

#include "audit/AuditAction.h"
#include "audit/AuditOutcome.h"
#include "foundation/EntityId.h"
#include "foundation/HashDigest.h"
#include "foundation/ServiceState.h"
#include "foundation/Timestamp.h"

#include <cstdint>
#include <string>

namespace aura {

struct AuditRecord {
    std::uint64_t sequence = 0;
    EntityId eventId;
    AuditAction action = AuditAction::UNKNOWN;
    AuditOutcome outcome = AuditOutcome::SUCCESS;
    ServiceState serviceState = ServiceState::ONLINE;
    Timestamp occurredAt;
    std::string actor;
    std::string subject;      // what the action targeted
    std::string details;
    HashDigest previousHash;  // hash chain link
    HashDigest recordHash;    // digest over this record's fields
};

}  // namespace aura
