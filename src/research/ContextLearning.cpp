// RSH-0006 - Context learning implementation.

#include "research/ContextLearning.h"

#include <algorithm>

namespace aura {

std::string ContextLearning::contextKey(const Outcome& outcome) {
    return std::string(toString(outcome.timeframe)) + "/" +
           toString(outcome.direction);
}

std::map<std::string, ContextBucket> ContextLearning::bucketize(
    const std::vector<Outcome>& outcomes) const {
    std::map<std::string, ContextBucket> buckets;
    for (const auto& outcome : outcomes) {
        ContextBucket& bucket = buckets[contextKey(outcome)];
        bucket.contextKey = contextKey(outcome);
        ++bucket.samples;
        if (outcome.classification == OutcomeClass::WIN) ++bucket.wins;
        if (outcome.classification == OutcomeClass::LOSS) ++bucket.losses;
        bucket.totalR += outcome.realizedR;
    }
    for (auto& kv : buckets) {
        ContextBucket& bucket = kv.second;
        if (bucket.samples > 0) {
            bucket.averageR = bucket.totalR / static_cast<double>(bucket.samples);
        }
        const std::size_t decided = bucket.wins + bucket.losses;
        bucket.winRate = decided > 0 ? static_cast<double>(bucket.wins) /
                                           static_cast<double>(decided)
                                     : 0.0;
        bucket.sufficientEvidence = bucket.samples >= config_.minSamplesForEvidence;
    }
    return buckets;
}

std::size_t ContextLearning::learn(const std::vector<Outcome>& outcomes) {
    if (store_ == nullptr) return 0;
    std::size_t learned = 0;
    for (const auto& kv : bucketize(outcomes)) {
        const ContextBucket& bucket = kv.second;
        if (!bucket.sufficientEvidence) continue;

        KnowledgeEntry entry;
        entry.entryId = EntityId("ctx-" + bucket.contextKey);
        entry.kind = KnowledgeKind::PATTERN;
        entry.subject = bucket.contextKey;
        entry.contextKey = bucket.contextKey;
        entry.evidenceCount = bucket.samples;
        // Confidence is a bounded function of sample size; never certainty.
        entry.confidence = std::min(0.9, static_cast<double>(bucket.samples) / 100.0);
        entry.claim = "samples=" + std::to_string(bucket.samples) +
                      " winRate=" + std::to_string(bucket.winRate) +
                      " avgR=" + std::to_string(bucket.averageR);
        store_->put(entry);
        ++learned;
    }
    return learned;
}

}  // namespace aura
