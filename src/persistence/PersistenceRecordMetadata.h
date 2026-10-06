#pragma once
// PER-0002 - Persistence contract: metadata attached to every stored record.
//
// The idempotency key lets a replay of the same logical write be recognized
// and collapsed instead of duplicated.

#include "foundation/EntityId.h"
#include "foundation/HashDigest.h"
#include "foundation/Timestamp.h"
#include "foundation/Version.h"

#include <cstdint>
#include <string>

namespace aura {

struct PersistenceRecordMetadata {
    EntityId recordId;
    std::string recordType;      // logical collection/stream name
    Version schemaVersion;
    std::uint64_t sequence = 0;
    Timestamp createdAt;
    Timestamp updatedAt;
    std::string idempotencyKey;  // deterministic key for write de-duplication
    HashDigest contentHash;
};

}  // namespace aura
