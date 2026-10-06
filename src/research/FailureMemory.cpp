// RSH-0010 - Failure memory implementation.

#include "research/FailureMemory.h"

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

std::string FailureMemory::fingerprintOf(FailureCategory category,
                                         const std::string& summary) {
    return std::string(toString(category)) + "::" + summary;
}

std::string FailureMemory::encode(const FailureRecord& record) const {
    std::ostringstream out;
    out << escape(record.failureId.value()) << "|" << toString(record.category) << "|"
        << escape(record.summary) << "|" << escape(record.detail) << "|"
        << escape(record.fingerprint) << "|" << record.occurrences << "|"
        << record.firstSeen.epochMillis() << "|" << record.lastSeen.epochMillis()
        << "|" << (record.resolved ? "1" : "0") << "|"
        << record.resolvedAt.epochMillis() << "|" << escape(record.resolution);
    return out.str();
}

bool FailureMemory::decode(const std::string& payload, FailureRecord& out) const {
    const auto parts = split(payload);
    if (parts.size() < 11) return false;
    out.failureId = EntityId(parts[0]);
    FailureCategory category = FailureCategory::UNKNOWN;
    for (FailureCategory candidate :
         {FailureCategory::DATA, FailureCategory::BRIDGE, FailureCategory::DECISION,
          FailureCategory::EXECUTION, FailureCategory::PERSISTENCE,
          FailureCategory::CONFIGURATION}) {
        if (parts[1] == toString(candidate)) category = candidate;
    }
    out.category = category;
    out.summary = parts[2];
    out.detail = parts[3];
    out.fingerprint = parts[4];
    try {
        out.occurrences = static_cast<std::size_t>(std::stoull(parts[5]));
        out.firstSeen = Timestamp::fromEpochMillis(std::stoll(parts[6]));
        out.lastSeen = Timestamp::fromEpochMillis(std::stoll(parts[7]));
        out.resolvedAt = Timestamp::fromEpochMillis(std::stoll(parts[9]));
    } catch (...) {
        return false;
    }
    out.resolved = parts[8] == "1";
    out.resolution = parts[10];
    return out.valid();
}

void FailureMemory::persist(const FailureRecord& record) {
    if (store_ == nullptr || !store_->isAvailable()) return;
    PersistenceRecordMetadata metadata;
    metadata.recordId = record.failureId;
    metadata.recordType = collection_;
    metadata.schemaVersion = Version{1, 0, 0};
    metadata.createdAt = record.firstSeen;
    metadata.updatedAt = record.lastSeen;
    metadata.idempotencyKey = record.failureId.value();
    store_->put(collection_, record.failureId.value(), encode(record), metadata);
}

EntityId FailureMemory::record(FailureCategory category,
                               const std::string& summary,
                               const std::string& detail, Timestamp occurredAt) {
    const Timestamp when = occurredAt.isUnknown() ? Timestamp::now() : occurredAt;
    const std::string fingerprint = fingerprintOf(category, summary);

    auto existing = byFingerprint_.find(fingerprint);
    if (existing != byFingerprint_.end()) {
        FailureRecord& record = records_[existing->second];
        ++record.occurrences;
        record.lastSeen = when;
        record.detail = detail;
        record.resolved = false;
        record.resolution.clear();
        persist(record);
        return record.failureId;
    }

    FailureRecord record;
    record.failureId = EntityId("failure-" + std::to_string(++sequence_));
    record.category = category;
    record.summary = summary;
    record.detail = detail;
    record.fingerprint = fingerprint;
    record.occurrences = 1;
    record.firstSeen = when;
    record.lastSeen = when;
    records_[record.failureId] = record;
    byFingerprint_[fingerprint] = record.failureId;
    persist(record);
    return record.failureId;
}

bool FailureMemory::resolve(const EntityId& failureId,
                            const std::string& resolution, Timestamp resolvedAt) {
    auto it = records_.find(failureId);
    if (it == records_.end()) return false;
    it->second.resolved = true;
    it->second.resolution = resolution;
    it->second.resolvedAt = resolvedAt.isUnknown() ? Timestamp::now() : resolvedAt;
    persist(it->second);
    return true;
}

bool FailureMemory::get(const EntityId& failureId, FailureRecord& out) const {
    auto it = records_.find(failureId);
    if (it == records_.end()) return false;
    out = it->second;
    return true;
}

std::vector<FailureRecord> FailureMemory::all() const {
    std::vector<FailureRecord> result;
    for (const auto& kv : records_) result.push_back(kv.second);
    return result;
}

std::vector<FailureRecord> FailureMemory::open() const {
    std::vector<FailureRecord> result;
    for (const auto& kv : records_) {
        if (!kv.second.resolved) result.push_back(kv.second);
    }
    return result;
}

std::size_t FailureMemory::loadFromStore() {
    if (store_ == nullptr || !store_->isAvailable()) return 0;
    std::size_t loaded = 0;
    for (const auto& key : store_->keys(collection_)) {
        std::string payload;
        PersistenceRecordMetadata metadata;
        if (store_->get(collection_, key, payload, metadata) != PersistenceStatus::OK) {
            continue;
        }
        FailureRecord record;
        if (decode(payload, record)) {
            records_[record.failureId] = record;
            byFingerprint_[record.fingerprint] = record.failureId;
            ++loaded;
        }
    }
    return loaded;
}

}  // namespace aura
