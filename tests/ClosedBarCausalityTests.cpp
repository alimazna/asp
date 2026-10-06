// TST-0002 - Closed-bar causality: current-bar exclusion, ordering, no lookahead.

#include "TestHarness.h"

#include "data/BarFinalizer.h"
#include "data/DataValidator.h"
#include "mt5/Mt5BridgeContract.h"

using namespace aura;

namespace {

constexpr std::int64_t kMinute = 60;
constexpr std::int64_t kM15 = 15 * kMinute;

// now = open time of the 4th (forming) M15 bar; only the first three are closed.
std::vector<Bar> threeClosedPlusOneForming() {
    std::vector<Bar> bars;
    for (int i = 0; i < 4; ++i) {
        const std::int64_t open = i * kM15;
        bars.push_back(test::makeBar(Timeframe::M15, open, 2000 + i, 2005 + i,
                                     1995 + i, 2001 + i));
    }
    return bars;
}

}  // namespace

TEST_CASE(candidate_bar_is_never_closed) {
    const std::vector<Bar> bars = threeClosedPlusOneForming();
    // The 4th bar opens at 3*kM15 and closes at 4*kM15; at now = 3*kM15 it is
    // still forming.
    const Timestamp now = Timestamp::fromEpochMillis(3 * kM15 * 1000);

    BarFinalizer finalizer;
    const FinalizedBars finalized = finalizer.finalize(bars, now);

    CHECK_EQ(finalized.closedBars.size(), static_cast<std::size_t>(3));
    CHECK_EQ(finalized.candidateCount, static_cast<std::size_t>(1));
    CHECK(finalized.hasCandidate);
    CHECK_EQ(finalized.closedBars.back().openTimeSec, 2 * kM15);
}

TEST_CASE(bar_closes_exactly_at_interval_end) {
    BarFinalizer finalizer;
    const Bar bar = test::makeBar(Timeframe::M15, 0, 2000, 2005, 1995, 2001);

    CHECK(!finalizer.isClosed(bar, Timestamp::fromEpochMillis((kM15 - 1) * 1000)));
    CHECK(finalizer.isClosed(bar, Timestamp::fromEpochMillis(kM15 * 1000)));
}

TEST_CASE(close_grace_delays_finalization) {
    const std::vector<Bar> bars = threeClosedPlusOneForming();
    BarFinalizer finalizer(60 * 1000);   // 1 minute grace
    const Timestamp now = Timestamp::fromEpochMillis(3 * kM15 * 1000);
    const FinalizedBars finalized = finalizer.finalize(bars, now);

    // The bar at 2*kM15 closes at 3*kM15, but with grace it is not yet final.
    CHECK_EQ(finalized.closedBars.size(), static_cast<std::size_t>(2));
}

TEST_CASE(out_of_order_sequence_is_rejected) {
    std::vector<Bar> bars;
    bars.push_back(test::makeBar(Timeframe::M15, 2 * kM15, 2000, 2005, 1995, 2001));
    bars.push_back(test::makeBar(Timeframe::M15, 1 * kM15, 2000, 2005, 1995, 2001));

    DataValidator validator;
    const SequenceValidation validation = validator.validateSequence(bars);
    CHECK(!validation.ok);
    CHECK(!validation.issues.empty());
}

TEST_CASE(duplicate_bar_is_detected) {
    std::vector<Bar> bars;
    bars.push_back(test::makeBar(Timeframe::M15, 1 * kM15, 2000, 2005, 1995, 2001));
    bars.push_back(test::makeBar(Timeframe::M15, 1 * kM15, 2000, 2005, 1995, 2001));

    DataValidator validator;
    const SequenceValidation validation = validator.validateSequence(bars);
    CHECK(!validation.ok);
}

TEST_CASE(future_dated_bar_is_flagged) {
    const Timestamp now = Timestamp::fromEpochMillis(1000 * 1000);
    const Bar bar = test::makeBar(Timeframe::M15, 10 * 1000, 2000, 2005, 1995, 2001);
    DataValidator validator;
    const BarValidation validation = validator.validateBar(bar, now);
    CHECK_EQ(static_cast<int>(validation.defect),
             static_cast<int>(BarDefect::FUTURE_DATED));
    CHECK(!validation.ok);
}
