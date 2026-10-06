#pragma once
// RSH-0007 - Contradiction engine.
//
// Detects knowledge entries that make opposing claims about the same subject.
// A contradiction is recorded as evidence; it does not silently overwrite or
// delete either claim.

#include "research/KnowledgeStore.h"

#include <string>
#include <vector>

namespace aura {

struct Contradiction {
    EntityId first;
    EntityId second;
    std::string subject;
    std::string detail;
};

class ContradictionEngine {
public:
    explicit ContradictionEngine(KnowledgeStore* store) : store_(store) {}

    // Find pairs of active entries on the same subject with materially
    // different claims. Deterministic ordering by entry id.
    std::vector<Contradiction> detect(double confidenceGap = 0.3) const;

    // Record detected contradictions as CONTRADICTION knowledge entries.
    std::size_t record(double confidenceGap = 0.3);

private:
    KnowledgeStore* store_;
};

}  // namespace aura
