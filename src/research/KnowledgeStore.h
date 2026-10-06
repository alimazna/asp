#pragma once
// RSH-0001 - Durable research knowledge store.
//
// Research knowledge is derived from recorded outcomes only. Nothing here can
// mutate the live strategy or grant execution authority; entries are evidence.

#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"
#include "persistence/IPersistenceStore.h"

#include <map>
#include <string>
#include <vector>

namespace aura {

enum class KnowledgeKind {
    OBSERVATION,
    PATTERN,
    FAILURE,
    HYPOTHESIS,
    CONTRADICTION,
    UNKNOWN,
};

inline const char* toString(KnowledgeKind k) noexcept {
    switch (k) {
        case KnowledgeKind::OBSERVATION:   return "OBSERVATION";
        case KnowledgeKind::PATTERN:       return "PATTERN";
        case KnowledgeKind::FAILURE:       return "FAILURE";
        case KnowledgeKind::HYPOTHESIS:    return "HYPOTHESIS";
        case KnowledgeKind::CONTRADICTION: return "CONTRADICTION";
        case KnowledgeKind::UNKNOWN:       return "UNKNOWN";
    }
    return "UNKNOWN";
}

struct KnowledgeEntry {
    EntityId entryId;
    KnowledgeKind kind = KnowledgeKind::UNKNOWN;
    std::string subject;
    std::string claim;
    double confidence = 0.0;         // 0..1, research confidence
    std::size_t evidenceCount = 0;
    std::string contextKey;          // e.g. regime/timeframe bucket
    Timestamp createdAt;
    Timestamp updatedAt;
    bool superseded = false;

    bool valid() const noexcept { return !entryId.empty(); }
};

class KnowledgeStore {
public:
    explicit KnowledgeStore(IPersistenceStore* store = nullptr,
                            std::string collection = "research_knowledge")
        : store_(store), collection_(std::move(collection)) {}

    bool put(const KnowledgeEntry& entry);
    bool get(const EntityId& entryId, KnowledgeEntry& out) const;

    bool supersede(const EntityId& entryId, const EntityId& byEntryId);

    std::vector<KnowledgeEntry> byKind(KnowledgeKind kind) const;
    std::vector<KnowledgeEntry> byContext(const std::string& contextKey) const;
    std::vector<KnowledgeEntry> all() const;

    // Reload from the durable store (restart recovery).
    std::size_t loadFromStore();

    std::size_t size() const noexcept { return entries_.size(); }

private:
    std::string encode(const KnowledgeEntry& entry) const;
    bool decode(const std::string& payload, KnowledgeEntry& out) const;

    IPersistenceStore* store_;
    std::string collection_;
    std::map<EntityId, KnowledgeEntry> entries_;
};

}  // namespace aura
