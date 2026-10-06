// REC-0004 - Checkpoint manager implementation.

#include "recovery/CheckpointManager.h"

#include "integrity/IHasher.h"

#include <sstream>

namespace aura {

namespace {

std::string escape(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '|' || c == '\\' || c == '\n') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '|') {
            parts.push_back(current);
            current.clear();
        } else if (text[i] == '\\' && i + 1 < text.size()) {
            current.push_back(text[++i]);
        } else {
            current.push_back(text[i]);
        }
    }
    parts.push_back(current);
    return parts;
}

}  // namespace

std::string CheckpointManager::encode(const Checkpoint& checkpoint) {
    std::ostringstream out;
    out << escape(checkpoint.name) << "|" << checkpoint.sequence << "|"
        << checkpoint.createdAt.epochMillis() << "|"
        << checkpoint.contentHash.hex() << "|" << escape(checkpoint.payload);
    return out.str();
}

bool CheckpointManager::decode(const std::string& line, Checkpoint& out) {
    const auto parts = split(line);
    if (parts.size() < 5) return false;
    out.name = parts[0];
    try {
        out.sequence = std::stoull(parts[1]);
        out.createdAt = Timestamp::fromEpochMillis(std::stoll(parts[2]));
    } catch (...) {
        return false;
    }
    out.contentHash = HashDigest("sha256", parts[3]);
    out.payload = parts[4];
    out.valid = true;
    return true;
}

bool CheckpointManager::isAvailable() const {
    return store_ != nullptr && store_->isAvailable();
}

bool CheckpointManager::checkpoint(const std::string& name,
                                   const std::string& payload, Timestamp now,
                                   std::uint64_t& outSequence) {
    if (!isAvailable()) return false;

    Checkpoint checkpoint;
    checkpoint.name = name;
    checkpoint.createdAt = now.isUnknown() ? Timestamp::now() : now;
    checkpoint.payload = payload;
    auto hasher = makeHasher(HashAlgorithm::SHA256);
    if (hasher) checkpoint.contentHash = hasher->hash(payload);

    PersistenceRecordMetadata metadata;
    metadata.recordType = stream_;
    metadata.schemaVersion = Version{1, 0, 0};
    metadata.createdAt = checkpoint.createdAt;
    metadata.updatedAt = checkpoint.createdAt;
    metadata.contentHash = checkpoint.contentHash;

    if (store_->append(stream_, encode(checkpoint), metadata, outSequence) !=
        PersistenceStatus::OK) {
        return false;
    }
    checkpoint.sequence = outSequence;
    return true;
}

bool CheckpointManager::latest(const std::string& name, Checkpoint& out) const {
    if (!isAvailable()) return false;
    bool found = false;
    for (const auto& line : store_->readStream(stream_)) {
        Checkpoint checkpoint;
        if (!decode(line, checkpoint)) continue;
        if (checkpoint.name != name) continue;
        if (!found || checkpoint.sequence > out.sequence) {
            out = checkpoint;
            found = true;
        }
    }
    return found;
}

std::size_t CheckpointManager::count(const std::string& name) const {
    if (!isAvailable()) return 0;
    std::size_t result = 0;
    for (const auto& line : store_->readStream(stream_)) {
        Checkpoint checkpoint;
        if (decode(line, checkpoint) && checkpoint.name == name) ++result;
    }
    return result;
}

}  // namespace aura
