// AUD-0005 - Append-only audit log implementation.

#include "audit/AuditLog.h"

#include <algorithm>

namespace aura {

namespace {

std::string encode(const AuditRecord& record) {
    // Deterministic canonical encoding of the chained fields. Timestamps use
    // epoch millis so the digest does not depend on formatting.
    std::string out;
    out += std::to_string(record.sequence);
    out += '|';
    out += record.eventId.value();
    out += '|';
    out += toString(record.action);
    out += '|';
    out += toString(record.outcome);
    out += '|';
    out += toString(record.serviceState);
    out += '|';
    out += std::to_string(record.occurredAt.epochMillis());
    out += '|';
    out += record.actor;
    out += '|';
    out += record.subject;
    out += '|';
    out += record.details;
    out += '|';
    out += record.previousHash.hex();
    return out;
}

}  // namespace

AuditLog::AuditLog() : hasher_(makeHasher(HashAlgorithm::FNV1A64)) {}

bool AuditLog::append(AuditRecord& record) {
    if (record.eventId.empty()) return false;
    if (contains(record.eventId)) return false;   // append-only, no rewrite

    record.sequence = static_cast<std::uint64_t>(records_.size()) + 1;
    record.previousHash = records_.empty() ? HashDigest() : records_.back().recordHash;
    const std::string payload = encode(record);
    if (hasher_) {
        record.recordHash = hasher_->hash(payload);
    } else {
        record.recordHash = HashDigest("NONE", payload);
    }
    records_.push_back(record);
    return true;
}

bool AuditLog::contains(const EntityId& eventId) const {
    for (const auto& record : records_) {
        if (record.eventId == eventId) return true;
    }
    return false;
}

std::vector<AuditRecord> AuditLog::recent(std::size_t limit) const {
    std::vector<AuditRecord> out;
    const std::size_t count = limit == 0 ? records_.size()
                                         : std::min(limit, records_.size());
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(records_[records_.size() - 1 - i]);   // newest first
    }
    return out;
}

}  // namespace aura
