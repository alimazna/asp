#pragma once
// RSH-0005 - Context learning.
//
// Aggregates recorded outcomes into context buckets (timeframe/regime/direction)
// and derives descriptive statistics. These are observations, not predictions,
// and never mutate the live strategy.

#include "outcomes/OutcomeEngine.h"
#include "research/KnowledgeStore.h"

#include <map>
#include <string>
#include <vector>

namespace aura {

struct ContextBucket {
    std::string contextKey;
    std::size_t samples = 0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    double totalR = 0.0;
    double averageR = 0.0;
    double winRate = 0.0;
    bool sufficientEvidence = false;
};

struct ContextLearningConfig {
    std::size_t minSamplesForEvidence = 10;
};

class ContextLearning {
public:
    ContextLearning(KnowledgeStore* store, ContextLearningConfig config = {})
        : store_(store), config_(config) {}

    // Build buckets from outcomes, keyed by timeframe+direction.
    std::map<std::string, ContextBucket> bucketize(
        const std::vector<Outcome>& outcomes) const;

    // Persist a PATTERN knowledge entry for each sufficiently-sampled bucket.
    std::size_t learn(const std::vector<Outcome>& outcomes);

    static std::string contextKey(const Outcome& outcome);

private:
    KnowledgeStore* store_;
    ContextLearningConfig config_;
};

}  // namespace aura
