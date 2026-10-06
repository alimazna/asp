// RSH-0014 - Research planner implementation.

#include "research/ResearchPlanner.h"

#include <algorithm>

namespace aura {

ResearchPlan ResearchPlanner::plan() const {
    ResearchPlan plan;
    if (hypotheses_ == nullptr) {
        plan.reason = "no hypothesis source";
        return plan;
    }

    std::vector<ResearchTask> candidates;
    for (const auto& hypothesis : hypotheses_->all()) {
        if (hypothesis.status != HypothesisStatus::PROPOSED &&
            hypothesis.status != HypothesisStatus::UNDER_TEST) {
            continue;
        }
        ResearchTask task;
        task.hypothesisId = hypothesis.hypothesisId;
        task.objective = "test: " + hypothesis.statement;
        task.method = "out-of-sample replay against recorded outcomes";
        task.priority = policy_.preferHighPriorConfidence
                            ? static_cast<int>(hypothesis.priorConfidence * 100.0)
                            : 0;
        candidates.push_back(task);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const ResearchTask& a, const ResearchTask& b) {
                  if (a.priority != b.priority) return a.priority > b.priority;
                  return a.hypothesisId.value() < b.hypothesisId.value();
              });

    if (candidates.size() > policy_.maxTasks) candidates.resize(policy_.maxTasks);
    plan.tasks = candidates;
    plan.valid = true;
    plan.reason = "planned " + std::to_string(candidates.size()) + " task(s)";
    return plan;
}

}  // namespace aura
