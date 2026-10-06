#pragma once
// DAT-0010 - Enforces closed-bar causality.
//
// Given a set of bars and a wall-clock `now`, the finalizer identifies the
// last bar whose interval has fully elapsed. The currently forming bar (the
// candidate) is never emitted as closed. This is the structural no-lookahead
// guarantee for the decision chain.

#include "data/BarNormalizer.h"
#include "foundation/Timestamp.h"

#include <cstddef>
#include <vector>

namespace aura {

struct FinalizedBars {
    std::vector<Bar> closedBars;    // bars with fully elapsed intervals
    std::size_t candidateCount = 0; // trailing bars not yet closed
    bool hasCandidate = false;
};

class BarFinalizer {
public:
    // `bars` is assumed ascending by openTime. A bar closes when
    // now >= openTime + interval + closeGraceMillis.
    explicit BarFinalizer(std::int64_t closeGraceMillis = 0)
        : closeGraceMillis_(closeGraceMillis) {}

    FinalizedBars finalize(const std::vector<Bar>& bars, Timestamp now) const;

    // True when `bar`'s interval has fully elapsed at `now`.
    bool isClosed(const Bar& bar, Timestamp now) const;

private:
    std::int64_t closeGraceMillis_;
};

}  // namespace aura
