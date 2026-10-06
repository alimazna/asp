#pragma once
// PER-0003 - Persistence contract: the store interface.
//
// Semantics:
//  * writes are transactional and idempotent at the record boundary;
//  * an append-only stream cannot be updated or deleted;
//  * reads never fabricate a missing record (NOT_FOUND is explicit).

#include "persistence/PersistenceRecordMetadata.h"
#include "persistence/PersistenceStatus.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aura {

class IPersistenceStore {
public:
    virtual ~IPersistenceStore() = default;

    // Key/value durable records.
    virtual PersistenceStatus put(const std::string& collection,
                                  const std::string& key,
                                  const std::string& payload,
                                  const PersistenceRecordMetadata& metadata) = 0;

    virtual PersistenceStatus get(const std::string& collection,
                                  const std::string& key,
                                  std::string& outPayload,
                                  PersistenceRecordMetadata& outMetadata) const = 0;

    virtual PersistenceStatus remove(const std::string& collection,
                                     const std::string& key) = 0;

    virtual bool contains(const std::string& collection,
                          const std::string& key) const = 0;

    virtual std::vector<std::string> keys(const std::string& collection) const = 0;

    // Append-only streams (audit/history). Returns the assigned sequence.
    virtual PersistenceStatus append(const std::string& stream,
                                     const std::string& payload,
                                     const PersistenceRecordMetadata& metadata,
                                     std::uint64_t& outSequence) = 0;

    virtual std::vector<std::string> readStream(const std::string& stream) const = 0;
    virtual std::size_t streamSize(const std::string& stream) const = 0;

    // Durability boundary: force pending writes to stable storage.
    virtual PersistenceStatus flush() = 0;

    virtual bool isAvailable() const = 0;
};

}  // namespace aura
