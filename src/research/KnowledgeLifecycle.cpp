// RSH-0004 - Knowledge lifecycle implementation.

#include "research/KnowledgeLifecycle.h"

namespace aura {

LifecycleStage KnowledgeLifecycle::stageOf(const KnowledgeEntry& entry) const {
    if (entry.superseded) return LifecycleStage::RETIRED;
    if (entry.confidence <= policy_.retireConfidence) return LifecycleStage::RETIRED;
    if (entry.confidence <= policy_.weakeningConfidence) return LifecycleStage::WEAKENING;
    if (entry.evidenceCount >= policy_.establishedEvidence) {
        return LifecycleStage::ESTABLISHED;
    }
    if (entry.evidenceCount >= policy_.supportedEvidence) {
        return LifecycleStage::SUPPORTED;
    }
    return LifecycleStage::NEW;
}

bool KnowledgeLifecycle::evaluate(const EntityId& entryId, double updatedConfidence,
                                  std::size_t evidenceCount) {
    if (store_ == nullptr) return false;
    KnowledgeEntry entry;
    if (!store_->get(entryId, entry)) return false;

    const LifecycleStage before = stageOf(entry);
    entry.confidence = updatedConfidence;
    entry.evidenceCount = evidenceCount;
    const LifecycleStage after = stageOf(entry);
    if (after == LifecycleStage::RETIRED) entry.superseded = true;
    store_->put(entry);
    return before != after;
}

}  // namespace aura
