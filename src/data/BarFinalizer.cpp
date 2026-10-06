// DAT-0011 - Closed-bar finalizer implementation.

#include "data/BarFinalizer.h"

namespace aura {

bool BarFinalizer::isClosed(const Bar& bar, Timestamp now) const {
    if (now.isUnknown()) return false;   // cannot assert closure without time
    // Negative open times are invalid; epoch 0 is a legitimate boundary bar.
    if (bar.openTimeSec < 0) return false;
    const std::int64_t closeMillis =
        bar.openTimeSec * 1000 + intervalMillis(bar.timeframe) + closeGraceMillis_;
    return now.epochMillis() >= closeMillis;
}

FinalizedBars BarFinalizer::finalize(const std::vector<Bar>& bars,
                                     Timestamp now) const {
    FinalizedBars result;
    for (const auto& bar : bars) {
        if (isClosed(bar, now)) {
            result.closedBars.push_back(bar);
        } else {
            ++result.candidateCount;
            result.hasCandidate = true;
        }
    }
    return result;
}

}  // namespace aura
