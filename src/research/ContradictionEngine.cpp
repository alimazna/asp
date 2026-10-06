// RSH-0008 - Contradiction engine implementation.

#include "research/ContradictionEngine.h"

#include <cmath>

namespace aura {

std::vector<Contradiction> ContradictionEngine::detect(double confidenceGap) const {
    std::vector<Contradiction> contradictions;
    if (store_ == nullptr) return contradictions;

    const auto entries = store_->all();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].superseded) continue;
        for (std::size_t j = i + 1; j < entries.size(); ++j) {
            if (entries[j].superseded) continue;
            if (entries[i].subject.empty() ||
                entries[i].subject != entries[j].subject) {
                continue;
            }
            if (entries[i].kind != entries[j].kind) continue;
            if (std::fabs(entries[i].confidence - entries[j].confidence) >=
                confidenceGap) {
                Contradiction contradiction;
                contradiction.first = entries[i].entryId;
                contradiction.second = entries[j].entryId;
                contradiction.subject = entries[i].subject;
                contradiction.detail =
                    "confidence " + std::to_string(entries[i].confidence) + " vs " +
                    std::to_string(entries[j].confidence);
                contradictions.push_back(contradiction);
            }
        }
    }
    return contradictions;
}

std::size_t ContradictionEngine::record(double confidenceGap) {
    if (store_ == nullptr) return 0;
    std::size_t recorded = 0;
    for (const auto& contradiction : detect(confidenceGap)) {
        KnowledgeEntry entry;
        entry.entryId = EntityId("contradiction-" + contradiction.first.value() + "-" +
                                 contradiction.second.value());
        if (store_->get(entry.entryId, entry)) continue;   // already recorded
        entry.kind = KnowledgeKind::CONTRADICTION;
        entry.subject = contradiction.subject;
        entry.claim = contradiction.detail;
        entry.contextKey = contradiction.subject;
        entry.confidence = 0.0;
        entry.evidenceCount = 2;
        store_->put(entry);
        ++recorded;
    }
    return recorded;
}

}  // namespace aura
