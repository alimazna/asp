// RSH-0012 - Hypothesis engine implementation.

#include "research/HypothesisEngine.h"

namespace aura {

bool HypothesisEngine::proposeFrom(const EntityId& knowledgeEntryId,
                                   const std::string& falsificationCondition) {
    if (store_ == nullptr) return false;
    KnowledgeEntry entry;
    if (!store_->get(knowledgeEntryId, entry)) return false;

    const EntityId hypothesisId("hypothesis-" + knowledgeEntryId.value());
    for (const auto& existing : hypotheses_) {
        if (existing.hypothesisId == hypothesisId) return true;   // idempotent
    }

    Hypothesis hypothesis;
    hypothesis.hypothesisId = hypothesisId;
    hypothesis.statement = "If " + entry.claim + " then " + entry.subject +
                           " behaves consistently out-of-sample";
    hypothesis.falsificationCondition = falsificationCondition;
    hypothesis.contextKey = entry.contextKey;
    hypothesis.priorConfidence = entry.confidence;
    hypothesis.status = HypothesisStatus::PROPOSED;
    hypothesis.createdAt = Timestamp::now();
    hypotheses_.push_back(hypothesis);
    return true;
}

bool HypothesisEngine::setStatus(const EntityId& hypothesisId,
                                 HypothesisStatus status) {
    for (auto& hypothesis : hypotheses_) {
        if (hypothesis.hypothesisId == hypothesisId) {
            hypothesis.status = status;
            return true;
        }
    }
    return false;
}

bool HypothesisEngine::get(const EntityId& hypothesisId, Hypothesis& out) const {
    for (const auto& hypothesis : hypotheses_) {
        if (hypothesis.hypothesisId == hypothesisId) {
            out = hypothesis;
            return true;
        }
    }
    return false;
}

std::vector<Hypothesis> HypothesisEngine::all() const { return hypotheses_; }

}  // namespace aura
