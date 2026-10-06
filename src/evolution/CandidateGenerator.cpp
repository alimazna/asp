// EVO-0002 - Candidate generator implementation.

#include "evolution/CandidateGenerator.h"

#include <algorithm>

namespace aura {

std::vector<Candidate> CandidateGenerator::generate(const EntityId& hypothesisId,
                                                    Timestamp now) {
    std::vector<Candidate> candidates;
    if (hypotheses_ == nullptr) return candidates;

    Hypothesis hypothesis;
    if (!hypotheses_->get(hypothesisId, hypothesis)) return candidates;

    // Generation must run inside the sandbox; refuse otherwise.
    if (sandbox_ == nullptr || !sandbox_->isOpen()) return candidates;
    if (!sandbox_->withinLimits(now)) return candidates;

    // A small, deterministic set of parameter variations around a neutral
    // baseline. No randomness: the same hypothesis yields the same candidates.
    const std::vector<std::pair<std::string, std::vector<double>>> axes = {
        {"entry_threshold", {40.0, 55.0, 70.0}},
        {"stop_atr_multiple", {1.0, 1.5, 2.0}},
        {"target_atr_multiple", {1.5, 2.5, 3.5}},
    };

    std::size_t index = 0;
    for (const auto& axis : axes) {
        for (double value : axis.second) {
            if (index >= limits_.maxCandidatesPerHypothesis) break;
            if (value < limits_.minParameterValue ||
                value > limits_.maxParameterValue) {
                continue;
            }
            if (!sandbox_->noteOperation()) break;

            Candidate candidate;
            candidate.candidateId =
                EntityId("cand-" + hypothesisId.value() + "-" +
                         std::to_string(index));
            candidate.originHypothesis = hypothesisId;
            candidate.label = hypothesis.contextKey + ":" + axis.first + "=" +
                              std::to_string(value);
            candidate.parameters[axis.first] = value;
            candidate.createdAt = now.isUnknown() ? Timestamp::now() : now;
            candidate.valid = true;
            candidates.push_back(candidate);
            ++index;
        }
    }
    return candidates;
}

std::vector<Candidate> CandidateGenerator::generateAll(Timestamp now) {
    std::vector<Candidate> all;
    if (hypotheses_ == nullptr) return all;
    for (const auto& hypothesis : hypotheses_->all()) {
        if (hypothesis.status != HypothesisStatus::PROPOSED &&
            hypothesis.status != HypothesisStatus::UNDER_TEST) {
            continue;
        }
        const auto generated = generate(hypothesis.hypothesisId, now);
        all.insert(all.end(), generated.begin(), generated.end());
    }
    return all;
}

}  // namespace aura
