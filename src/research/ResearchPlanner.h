#pragma once
// RSH-0013 - Research planner.
//
// Prioritises hypotheses into a bounded research queue. Planning is
// deterministic given the same inputs and never exceeds the research budget.

#include "research/HypothesisEngine.h"

#include <string>
#include <vector>

namespace aura {

struct ResearchTask {
    EntityId hypothesisId;
    int priority = 0;             // higher runs first
    std::string objective;
    std::string method;
};

struct ResearchPlan {
    std::vector<ResearchTask> tasks;
    bool valid = false;
    std::string reason;
};

struct PlannerPolicy {
    std::size_t maxTasks = 5;
    bool preferHighPriorConfidence = true;
};

class ResearchPlanner {
public:
    explicit ResearchPlanner(const HypothesisEngine* hypotheses,
                             PlannerPolicy policy = {})
        : hypotheses_(hypotheses), policy_(policy) {}

    ResearchPlan plan() const;

private:
    const HypothesisEngine* hypotheses_;
    PlannerPolicy policy_;
};

}  // namespace aura
