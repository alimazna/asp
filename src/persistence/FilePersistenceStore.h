#pragma once
// Persistence infrastructure: a durable, file-backed IPersistenceStore.
//
// This is the concrete store the runtime hands to PersistenceEngine,
// CheckpointManager, and the research/evolution registries. It is intentionally
// simple and dependency-free so the desktop package needs nothing beyond the
// standard library. Guarantees:
//  * writes are durable at the record boundary (flush/fsync on demand);
//  * append-only streams are never rewritten or truncated;
//  * a missing record reports NOT_FOUND, never an empty/safe value;
//  * a re-put of identical content is an idempotent no-op.

#include "persistence/IPersistenceStore.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace aura {

class FilePersistenceStore : public IPersistenceStore {
public:
    explicit FilePersistenceStore(std::string rootDirectory,
                                  bool createIfMissing = true);

    PersistenceStatus put(const std::string& collection,
                          const std::string& key,
                          const std::string& payload,
                          const PersistenceRecordMetadata& metadata) override;

    PersistenceStatus get(const std::string& collection,
                          const std::string& key,
                          std::string& outPayload,
                          PersistenceRecordMetadata& outMetadata) const override;

    PersistenceStatus remove(const std::string& collection,
                             const std::string& key) override;

    bool contains(const std::string& collection,
                  const std::string& key) const override;

    std::vector<std::string> keys(const std::string& collection) const override;

    PersistenceStatus append(const std::string& stream,
                             const std::string& payload,
                             const PersistenceRecordMetadata& metadata,
                             std::uint64_t& outSequence) override;

    std::vector<std::string> readStream(const std::string& stream) const override;
    std::size_t streamSize(const std::string& stream) const override;

    PersistenceStatus flush() override;

    bool isAvailable() const override;

    const std::string& root() const noexcept { return root_; }

    // Diagnostics: last error observed by the store.
    const std::string& lastError() const noexcept { return lastError_; }

private:
    struct StoredRecord {
        std::string payload;
        PersistenceRecordMetadata metadata;
    };

    std::string kvPath(const std::string& collection) const;
    std::string streamPath(const std::string& stream) const;

    bool loadKv(const std::string& collection,
                std::map<std::string, StoredRecord>& out) const;
    bool saveKv(const std::string& collection,
                const std::map<std::string, StoredRecord>& records);

    mutable std::mutex mutex_;
    std::string root_;
    bool available_ = false;
    mutable std::string lastError_;
};

}  // namespace aura
