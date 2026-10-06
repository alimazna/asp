#pragma once
// REC-0003 - Checkpoint manager.
//
// Persists named checkpoints of runtime state so a crash or restart can resume
// from a known point. Checkpoints are append-only and never overwrite a prior
// checkpoint's identity; the latest pointer may move forward only.

#include "persistence/IPersistenceStore.h"

#include <cstdint>
#include <string>
#include <vector>

namespace aura {

struct Checkpoint {
    std::string name;
    std::uint64_t sequence = 0;
    Timestamp createdAt;
    std::string payload;
    HashDigest contentHash;
    bool valid = false;
};

class CheckpointManager {
public:
    explicit CheckpointManager(IPersistenceStore* store,
                               std::string stream = "checkpoints")
        : store_(store), stream_(std::move(stream)) {}

    // Write a checkpoint. Returns false when the store is unavailable; a
    // checkpoint is never faked when it cannot be durably written.
    bool checkpoint(const std::string& name, const std::string& payload,
                    Timestamp now, std::uint64_t& outSequence);

    // Load the most recent checkpoint for a name. Returns false if none.
    bool latest(const std::string& name, Checkpoint& out) const;

    std::size_t count(const std::string& name) const;

    bool isAvailable() const;

private:
    static std::string encode(const Checkpoint& checkpoint);
    static bool decode(const std::string& line, Checkpoint& out);

    IPersistenceStore* store_;
    std::string stream_;
};

}  // namespace aura
