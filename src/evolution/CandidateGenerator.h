#pragma once
// EVO-0001 - Candidate generator.
//
// Proposes bounded strategy-parameter variations for a hypothesis. Generation
// runs inside a research sandbox and produces candidates only; it never
// activates a candidate or grants it execution authority.

#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"
#include "research/HypothesisEngine.h"
#include "research/Sandbox.h"

#include <map>
#include <string>
#include <vector>

namespace aura {

struct Candidate {
    EntityId candidateId;
    EntityId originHypothesis;
    std::string label;
    std::map<std::string, double> parameters;
    Timestamp createdAt;
    bool active = false;        // activation is a governance act, never here
    bool valid = false;

    bool isValid() const noexcept { return valid && !candidateId.empty(); }
};

struct GeneratorLimits {
    std::size_t maxCandidatesPerHypothesis = 8;
    double minParameterValue = 0.0;
    double maxParameterValue = 100.0;
};

class CandidateGenerator {
public:
    CandidateGenerator(const HypothesisEngine* hypotheses, Sandbox* sandbox,
                       GeneratorLimits limits = {})
        : hypotheses_(hypotheses), sandbox_(sandbox), limits_(limits) {}

    // Generate candidates for one hypothesis. Deterministic given the same
    // hypothesis and a fresh sandbox. Returns an empty vector on refusal.
    std::vector<Candidate> generate(const EntityId& hypothesisId,
                                    Timestamp now);

    // Generate for every proposed/under-test hypothesis (bounded).
    std::vector<Candidate> generateAll(Timestamp now);

    const GeneratorLimits& limits() const noexcept { return limits_; }

private:
    const HypothesisEngine* hypotheses_;
    Sandbox* sandbox_;
    GeneratorLimits limits_;
};

}  // namespace aura
