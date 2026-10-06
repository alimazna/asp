// RSH-0002 - Knowledge store implementation.

#include "research/KnowledgeStore.h"

#include <sstream>

namespace aura {

namespace {

std::string escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (c == '|' || c == '\n' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::string unescape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            out.push_back(in[++i]);
        } else {
            out.push_back(in[i]);
        }
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

std::string KnowledgeStore::encode(const KnowledgeEntry& entry) const {
    std::ostringstream out;
    out << escape(entry.entryId.value()) << "|" << toString(entry.kind) << "|"
        << escape(entry.subject) << "|" << escape(entry.claim) << "|"
        << entry.confidence << "|" << entry.evidenceCount << "|"
        << escape(entry.contextKey) << "|" << entry.createdAt.epochMillis() << "|"
        << entry.updatedAt.epochMillis() << "|"
        << (entry.superseded ? "1" : "0");
    return out.str();
}

bool KnowledgeStore::decode(const std::string& payload, KnowledgeEntry& out) const {
    const auto parts = split(payload);
    if (parts.size() < 10) return false;
    out.entryId = EntityId(unescape(parts[0]));
    KnowledgeKind kind = KnowledgeKind::UNKNOWN;
    for (KnowledgeKind candidate :
         {KnowledgeKind::OBSERVATION, KnowledgeKind::PATTERN, KnowledgeKind::FAILURE,
          KnowledgeKind::HYPOTHESIS, KnowledgeKind::CONTRADICTION}) {
        if (parts[1] == toString(candidate)) kind = candidate;
    }
    out.kind = kind;
    out.subject = unescape(parts[2]);
    out.claim = unescape(parts[3]);
    try {
        out.confidence = std::stod(parts[4]);
        out.evidenceCount = static_cast<std::size_t>(std::stoull(parts[5]));
        out.createdAt = Timestamp::fromEpochMillis(std::stoll(parts[7]));
        out.updatedAt = Timestamp::fromEpochMillis(std::stoll(parts[8]));
    } catch (...) {
        return false;
    }
    out.contextKey = unescape(parts[6]);
    out.superseded = parts[9] == "1";
    return out.valid();
}

bool KnowledgeStore::put(const KnowledgeEntry& entry) {
    if (!entry.valid()) return false;
    KnowledgeEntry stored = entry;
    if (stored.createdAt.isUnknown()) stored.createdAt = Timestamp::now();
    stored.updatedAt = Timestamp::now();
    entries_[stored.entryId] = stored;

    if (store_ != nullptr && store_->isAvailable()) {
        PersistenceRecordMetadata metadata;
        metadata.recordId = stored.entryId;
        metadata.recordType = collection_;
        metadata.schemaVersion = Version{1, 0, 0};
        metadata.createdAt = stored.createdAt;
        metadata.updatedAt = stored.updatedAt;
        metadata.idempotencyKey = stored.entryId.value();
        const PersistenceStatus status =
            store_->put(collection_, stored.entryId.value(), encode(stored), metadata);
        return isPersistenceSuccess(status);
    }
    return true;
}

bool KnowledgeStore::get(const EntityId& entryId, KnowledgeEntry& out) const {
    auto it = entries_.find(entryId);
    if (it == entries_.end()) return false;
    out = it->second;
    return true;
}

bool KnowledgeStore::supersede(const EntityId& entryId, const EntityId& byEntryId) {
    auto it = entries_.find(entryId);
    if (it == entries_.end()) return false;
    if (entries_.find(byEntryId) == entries_.end()) return false;
    it->second.superseded = true;
    it->second.updatedAt = Timestamp::now();
    put(it->second);
    return true;
}

std::vector<KnowledgeEntry> KnowledgeStore::byKind(KnowledgeKind kind) const {
    std::vector<KnowledgeEntry> result;
    for (const auto& kv : entries_) {
        if (kv.second.kind == kind && !kv.second.superseded) result.push_back(kv.second);
    }
    return result;
}

std::vector<KnowledgeEntry> KnowledgeStore::byContext(
    const std::string& contextKey) const {
    std::vector<KnowledgeEntry> result;
    for (const auto& kv : entries_) {
        if (kv.second.contextKey == contextKey && !kv.second.superseded) {
            result.push_back(kv.second);
        }
    }
    return result;
}

std::vector<KnowledgeEntry> KnowledgeStore::all() const {
    std::vector<KnowledgeEntry> result;
    for (const auto& kv : entries_) result.push_back(kv.second);
    return result;
}

std::size_t KnowledgeStore::loadFromStore() {
    if (store_ == nullptr || !store_->isAvailable()) return 0;
    std::size_t loaded = 0;
    for (const auto& key : store_->keys(collection_)) {
        std::string payload;
        PersistenceRecordMetadata metadata;
        if (store_->get(collection_, key, payload, metadata) != PersistenceStatus::OK) {
            continue;
        }
        KnowledgeEntry entry;
        if (decode(payload, entry)) {
            entries_[entry.entryId] = entry;
            ++loaded;
        }
    }
    return loaded;
}

}  // namespace aura
