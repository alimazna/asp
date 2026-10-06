#pragma once
// RSH-0011 - Hypothesis engine.
//
// Turns observed patterns and failures into testable hypotheses. A hypothesis
// is a claim with an explicit falsification condition; it carries no
// execution authority and cannot alter the live strategy.

#include "research/KnowledgeStore.h"

#include <string>
#include <vector>

namespace aura {

enum class HypothesisStatus {
    PROPOSED,
    UNDER_TEST,
    SUPPORTED,
    FALSIFIED,
    ABANDONED,
};

inline const char* toString(HypothesisStatus s) noexcept {
    switch (s) {
        case HypothesisStatus::PROPOSED:   return "PROPOSED";
        case HypothesisStatus::UNDER_TEST: return "UNDER_TEST";
        case HypothesisStatus::SUPPORTED:  return "SUPPORTED";
        case HypothesisStatus::FALSIFIED:  return "FALSIFIED";
        case HypothesisStatus::ABANDONED:  return "ABANDONED";
    }
    return "PROPOSED";
}

struct Hypothesis {
    EntityId hypothesisId;
    std::string statement;
    std::string falsificationCondition;
    std::string contextKey;
    HypothesisStatus status = HypothesisStatus::PROPOSED;
    double priorConfidence = 0.0;
    std::size_t testsRun = 0;
    Timestamp createdAt;

    bool valid() const noexcept { return !hypothesisId.empty(); }
};

class HypothesisEngine {
public:
    explicit HypothesisEngine(KnowledgeStore* store) : store_(store) {}

    // Propose a hypothesis from a knowledge entry. Idempotent by entry id.
    bool proposeFrom(const EntityId& knowledgeEntryId,
                     const std::string& falsificationCondition);

    bool setStatus(const EntityId& hypothesisId, HypothesisStatus status);

    bool get(const EntityId& hypothesisId, Hypothesis& out) const;
    std::vector<Hypothesis> all() const;

private:
    KnowledgeStore* store_;
    std::vector<Hypothesis> hypotheses_;
};

}  // namespace aura
