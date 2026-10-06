// VAL-0006 - Walk-forward validator implementation.

#include "validation/WalkForwardValidator.h"

namespace aura {

std::vector<WalkForwardWindow> WalkForwardValidator::windows(
    const std::vector<Bar>& closedBars, const ReplayEngine& engine) const {
    std::vector<WalkForwardWindow> result;
    if (config_.trainBars == 0 || config_.testBars == 0) return result;

    const std::size_t step = config_.testBars;
    for (std::size_t trainStart = 0;
         trainStart + config_.trainBars + config_.testBars <= closedBars.size();
         trainStart += step) {
        WalkForwardWindow window;
        window.trainStart = trainStart;
        window.trainEnd = trainStart + config_.trainBars;
        window.testStart = window.trainEnd;
        window.testEnd = window.testStart + config_.testBars;

        std::vector<Bar> test(closedBars.begin() + window.testStart,
                              closedBars.begin() + window.testEnd);
        const ReplayReport report = engine.replay(test);
        window.decisionsProduced = report.valid ? report.decisionsProduced : 0;
        result.push_back(window);
    }
    return result;
}

ValidationEvidence WalkForwardValidator::validate(
    const EntityId& candidateId, const std::vector<Bar>& closedBars,
    const ReplayEngine& engine) const {
    ValidationEvidence evidence;
    evidence.kind = ValidationKind::WALK_FORWARD;
    evidence.candidateId = candidateId;
    evidence.recordedAt = Timestamp::now();

    const auto windows = this->windows(closedBars, engine);
    evidence.samples = windows.size();
    evidence.sufficientEvidence = windows.size() >= config_.minWindows;
    if (windows.empty()) {
        evidence.detail = "no walk-forward windows fit the data";
        return evidence;
    }

    std::size_t emptyWindows = 0;
    std::size_t totalDecisions = 0;
    for (const auto& window : windows) {
        totalDecisions += window.decisionsProduced;
        if (window.decisionsProduced == 0) ++emptyWindows;
    }
    const double failureFraction =
        static_cast<double>(emptyWindows) / static_cast<double>(windows.size());
    evidence.metric = failureFraction;
    evidence.passed = evidence.sufficientEvidence &&
                      failureFraction <= config_.maxFailureFraction;
    evidence.detail = "windows=" + std::to_string(windows.size()) +
                      " empty=" + std::to_string(emptyWindows) +
                      " decisions=" + std::to_string(totalDecisions);
    return evidence;
}

}  // namespace aura
