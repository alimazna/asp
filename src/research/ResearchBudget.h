#pragma once
// RSH-0017 - Research budget.
//
// Bounds how much research may run: a maximum number of experiments and a
// maximum total compute allowance per window. Exhaustion stops research; it
// never borrows from execution resources.

#include "foundation/Timestamp.h"
#include "research/ResearchPlanner.h"

#include <cstdint>
#include <string>

namespace aura {

struct ResearchBudgetConfig {
    std::size_t maxExperimentsPerWindow = 20;
    std::int64_t maxComputeMillisPerWindow = 600000;   // 10 minutes
    std::int64_t windowMillis = 3600000;               // 1 hour
};

struct BudgetStatus {
    std::size_t experimentsUsed = 0;
    std::size_t experimentsRemaining = 0;
    std::int64_t computeUsedMillis = 0;
    std::int64_t computeRemainingMillis = 0;
    bool exhausted = false;
    std::string reason;
};

class ResearchBudget {
public:
    explicit ResearchBudget(ResearchBudgetConfig config = {}) : config_(config) {}

    // Attempt to reserve budget for one experiment. Returns false when the
    // budget is exhausted; no partial reservation is kept on failure.
    bool reserve(Timestamp now);

    // Account for compute consumed by a completed experiment.
    void recordCompute(std::int64_t millis);

    BudgetStatus status(Timestamp now) const;

    // Plan is admitted only if the remaining experiment budget covers it.
    bool admit(const ResearchPlan& plan, Timestamp now) const;

    const ResearchBudgetConfig& config() const noexcept { return config_; }

private:
    void rollWindowIfNeeded(Timestamp now) const;

    ResearchBudgetConfig config_;
    mutable Timestamp windowStart_;
    mutable std::size_t experimentsUsed_ = 0;
    mutable std::int64_t computeUsedMillis_ = 0;
};

}  // namespace aura
