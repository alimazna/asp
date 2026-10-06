#pragma once
// RSH-0009 - Failure memory.
//
// Durable memory of failures (data, bridge, decision, execution, persistence).
// A failure is never deleted; it is closed with a resolution note. Recurrence
// is counted so repeated failures are visible.

#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"
#include "persistence/IPersistenceStore.h"

#include <map>
#include <string>
#include <vector>

namespace aura {

enum class FailureCategory {
    DATA,
    BRIDGE,
    DECISION,
    EXECUTION,
    PERSISTENCE,
    CONFIGURATION,
    UNKNOWN,
};

inline const char* toString(FailureCategory c) noexcept {
    switch (c) {
        case FailureCategory::DATA:          return "DATA";
        case FailureCategory::BRIDGE:        return "BRIDGE";
        case FailureCategory::DECISION:      return "DECISION";
        case FailureCategory::EXECUTION:     return "EXECUTION";
        case FailureCategory::PERSISTENCE:   return "PERSISTENCE";
        case FailureCategory::CONFIGURATION: return "CONFIGURATION";
        case FailureCategory::UNKNOWN:       return "UNKNOWN";
    }
    return "UNKNOWN";
}

struct FailureRecord {
    EntityId failureId;
    FailureCategory category = FailureCategory::UNKNOWN;
    std::string summary;
    std::string detail;
    std::string fingerprint;   // stable identity for recurrence counting
    std::size_t occurrences = 1;
    Timestamp firstSeen;
    Timestamp lastSeen;
    bool resolved = false;
    Timestamp resolvedAt;
    std::string resolution;

    bool valid() const noexcept { return !failureId.empty(); }
};

class FailureMemory {
public:
    explicit FailureMemory(IPersistenceStore* store = nullptr,
                           std::string collection = "failure_memory")
        : store_(store), collection_(std::move(collection)) {}

    // Record a failure. Recurrence of the same fingerprint increments the
    // existing record rather than creating a duplicate.
    EntityId record(FailureCategory category, const std::string& summary,
                    const std::string& detail, Timestamp occurredAt);

    bool resolve(const EntityId& failureId, const std::string& resolution,
                 Timestamp resolvedAt);

    bool get(const EntityId& failureId, FailureRecord& out) const;
    std::vector<FailureRecord> all() const;
    std::vector<FailureRecord> open() const;

    std::size_t loadFromStore();

    std::size_t size() const noexcept { return records_.size(); }

private:
    static std::string fingerprintOf(FailureCategory category,
                                     const std::string& summary);
    std::string encode(const FailureRecord& record) const;
    bool decode(const std::string& payload, FailureRecord& out) const;
    void persist(const FailureRecord& record);

    IPersistenceStore* store_;
    std::string collection_;
    std::map<EntityId, FailureRecord> records_;
    std::map<std::string, EntityId> byFingerprint_;
    std::uint64_t sequence_ = 0;
};

}  // namespace aura
