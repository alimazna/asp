#pragma once
// RSH-0003 - Knowledge lifecycle.
//
// Governs how knowledge ages, gains or loses confidence, and is retired.
// Lifecycle transitions are evidence-driven; knowledge is never promoted to
// execution authority.

#include "research/KnowledgeStore.h"

#include <string>
#include <vector>

namespace aura {

enum class LifecycleStage {
    NEW,
    SUPPORTED,
    ESTABLISHED,
    WEAKENING,
    RETIRED,
};

inline const char* toString(LifecycleStage s) noexcept {
    switch (s) {
        case LifecycleStage::NEW:         return "NEW";
        case LifecycleStage::SUPPORTED:   return "SUPPORTED";
        case LifecycleStage::ESTABLISHED: return "ESTABLISHED";
        case LifecycleStage::WEAKENING:   return "WEAKENING";
        case LifecycleStage::RETIRED:     return "RETIRED";
    }
    return "NEW";
}

struct LifecyclePolicy {
    std::size_t supportedEvidence = 5;
    std::size_t establishedEvidence = 20;
    double weakeningConfidence = 0.35;
    double retireConfidence = 0.15;
};

class KnowledgeLifecycle {
public:
    explicit KnowledgeLifecycle(KnowledgeStore* store, LifecyclePolicy policy = {})
        : store_(store), policy_(policy) {}

    LifecycleStage stageOf(const KnowledgeEntry& entry) const;

    // Re-evaluate and persist a stage transition. Returns true when changed.
    bool evaluate(const EntityId& entryId, double updatedConfidence,
                  std::size_t evidenceCount);

    const LifecyclePolicy& policy() const noexcept { return policy_; }

private:
    KnowledgeStore* store_;
    LifecyclePolicy policy_;
};

}  // namespace aura
