// RSH-0017 - Research budget implementation.

#include "research/ResearchBudget.h"

namespace aura {

void ResearchBudget::rollWindowIfNeeded(Timestamp now) const {
    if (windowStart_.isUnknown() ||
        now.epochMillis() - windowStart_.epochMillis() >= config_.windowMillis) {
        windowStart_ = now;
        experimentsUsed_ = 0;
        computeUsedMillis_ = 0;
    }
}

bool ResearchBudget::reserve(Timestamp now) {
    rollWindowIfNeeded(now);
    if (experimentsUsed_ >= config_.maxExperimentsPerWindow) return false;
    if (computeUsedMillis_ >= config_.maxComputeMillisPerWindow) return false;
    ++experimentsUsed_;
    return true;
}

void ResearchBudget::recordCompute(std::int64_t millis) {
    if (millis <= 0) return;
    computeUsedMillis_ += millis;
}

BudgetStatus ResearchBudget::status(Timestamp now) const {
    rollWindowIfNeeded(now);
    BudgetStatus status;
    status.experimentsUsed = experimentsUsed_;
    status.experimentsRemaining =
        experimentsUsed_ >= config_.maxExperimentsPerWindow
            ? 0
            : config_.maxExperimentsPerWindow - experimentsUsed_;
    status.computeUsedMillis = computeUsedMillis_;
    status.computeRemainingMillis =
        computeUsedMillis_ >= config_.maxComputeMillisPerWindow
            ? 0
            : config_.maxComputeMillisPerWindow - computeUsedMillis_;
    status.exhausted = status.experimentsRemaining == 0 ||
                       status.computeRemainingMillis == 0;
    status.reason = status.exhausted ? "research budget exhausted" : "within budget";
    return status;
}

bool ResearchBudget::admit(const ResearchPlan& plan, Timestamp now) const {
    if (!plan.valid) return false;
    const BudgetStatus current = status(now);
    if (current.exhausted) return false;
    return plan.tasks.size() <= current.experimentsRemaining;
}

}  // namespace aura
