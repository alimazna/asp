// Agent-A (T01) - Feature leakage (no-lookahead) tests.
//
// Proves that a feature computed as of decision bar T is a pure function of
// bars closed at or before T: appending, mutating, or replacing bars after T
// must not change the result. This is the RULE 4 (no lookahead) evidence.

#include "TestHarness.h"

#include "analysis/features/AnalyticalFeatureEngine.h"

#include <map>
#include <string>
#include <vector>

using namespace aura;

namespace {

std::vector<Bar> series(Timeframe tf, std::size_t n, double start, double step) {
    std::vector<Bar> bars;
    const std::int64_t iv = intervalMillis(tf) / 1000;
    for (std::size_t i = 0; i < n; ++i) {
        const double o = start + step * static_cast<double>(i);
        const double c = o + step * 0.5;
        bars.push_back(test::makeBar(tf, static_cast<std::int64_t>(i) * iv, o,
                                     std::max(o, c) + 0.3, std::min(o, c) - 0.3,
                                     c));
    }
    return bars;
}

bool sameFeatures(const TimeframeFeatures& a, const TimeframeFeatures& b) {
    return a.asOfBarOpenSec == b.asOfBarOpenSec &&
           a.barsAvailable == b.barsAvailable && a.windowUsed == b.windowUsed &&
           a.structureTrend == b.structureTrend &&
           a.patternScore == b.patternScore &&
           a.momentumNorm == b.momentumNorm &&
           a.momentumPersistence == b.momentumPersistence &&
           a.momentumAcceleration == b.momentumAcceleration &&
           a.volatilityRatio == b.volatilityRatio && a.atrRatio == b.atrRatio &&
           a.rangePosition == b.rangePosition &&
           a.swingAsymmetry == b.swingAsymmetry && a.runBalance == b.runBalance &&
           a.contextTrend == b.contextTrend &&
           a.contextVolatility == b.contextVolatility &&
           a.contextRangePosition == b.contextRangePosition &&
           a.valid == b.valid && a.quality == b.quality;
}

}  // namespace

TEST_CASE(future_bars_do_not_change_features) {
    const AnalyticalFeatureEngine engine;
    const Timeframe tf = Timeframe::M15;
    const std::int64_t iv = intervalMillis(tf) / 1000;

    auto bars = series(tf, 60, 1900.0, 0.7);
    const std::int64_t decisionOpen = bars[39].openTimeSec;  // bar index 39

    const TimeframeFeatures before =
        engine.computeTimeframe(bars, tf, decisionOpen);

    // Append wild future bars whose values would move every feature if seen.
    for (std::size_t i = 60; i < 80; ++i) {
        const double base = 5000.0 + 100.0 * static_cast<double>(i);
        bars.push_back(test::makeBar(tf, static_cast<std::int64_t>(i) * iv, base,
                                     base + 500.0, base - 500.0, base + 400.0));
    }

    const TimeframeFeatures after =
        engine.computeTimeframe(bars, tf, decisionOpen);

    CHECK(sameFeatures(before, after));
    CHECK_EQ(after.asOfBarOpenSec, decisionOpen);
}

TEST_CASE(as_of_equals_truncated_prefix) {
    const AnalyticalFeatureEngine engine;
    const Timeframe tf = Timeframe::H4;
    auto bars = series(tf, 80, 1800.0, 1.3);
    const std::size_t k = 44;
    const std::int64_t decisionOpen = bars[k].openTimeSec;

    // Full series, pinned to bar k.
    const TimeframeFeatures pinned =
        engine.computeTimeframe(bars, tf, decisionOpen);
    // Only the causal prefix, default decision bar (the last one).
    const std::vector<Bar> prefix(bars.begin(), bars.begin() + (k + 1));
    const TimeframeFeatures prefixFeatures = engine.computeTimeframe(prefix, tf);

    CHECK(sameFeatures(pinned, prefixFeatures));
}

TEST_CASE(mutating_future_bars_is_irrelevant) {
    const AnalyticalFeatureEngine engine;
    const Timeframe tf = Timeframe::M5;
    auto bars = series(tf, 50, 1900.0, 0.4);
    const std::int64_t decisionOpen = bars[29].openTimeSec;
    const TimeframeFeatures base =
        engine.computeTimeframe(bars, tf, decisionOpen);

    // Overwrite every bar after the decision bar with degenerate values.
    for (std::size_t i = 30; i < bars.size(); ++i) {
        bars[i].open = 1.0;
        bars[i].high = 1.0;
        bars[i].low = 1.0;
        bars[i].close = 1.0;
    }
    const TimeframeFeatures mutated =
        engine.computeTimeframe(bars, tf, decisionOpen);
    CHECK(sameFeatures(base, mutated));
}

TEST_CASE(cross_timeframe_is_causal) {
    const AnalyticalFeatureEngine engine;

    std::map<Timeframe, std::vector<Bar>> all;
    all[Timeframe::M15] = series(Timeframe::M15, 40, 1900.0, 0.6);
    all[Timeframe::H4] = series(Timeframe::H4, 40, 1900.0, 1.9);
    all[Timeframe::D1] = series(Timeframe::D1, 40, 1900.0, 4.0);

    // computeCross uses the M15 tail; the wild D1 future must not leak into
    // the M15/H4 agreement signals.
    const CrossTimeframeFeatures before = engine.computeCross(all);

    for (auto& b : all[Timeframe::D1]) {
        b.close = 9999.0;
        b.high = 10000.0;
    }
    const CrossTimeframeFeatures after = engine.computeCross(all);

    // H4/M15 agreement is unchanged by D1 mutation; conflict share changes
    // only through the legitimately-supplied D1 stream, never through M15/H4.
    CHECK_EQ(before.h4M15Agreement, after.h4M15Agreement);
    CHECK_EQ(before.h4StructuralAuthority, after.h4StructuralAuthority);
    CHECK_EQ(before.m15TriggerState, after.m15TriggerState);
}

TEST_CASE(decision_bar_is_the_last_read_bar) {
    const AnalyticalFeatureEngine engine;
    const Timeframe tf = Timeframe::M30;
    auto bars = series(tf, 70, 1900.0, 0.5);
    const std::int64_t decisionOpen = bars[50].openTimeSec;
    const TimeframeFeatures f = engine.computeTimeframe(bars, tf, decisionOpen);
    // barsAvailable counts exactly the causal prefix [0..50].
    CHECK_EQ(f.barsAvailable, std::size_t{51});
    CHECK_EQ(f.asOfBarOpenSec, decisionOpen);
}
