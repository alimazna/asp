// Agent-A (T01) - Feature leakage (no-lookahead) tests.
//
// Proves that a feature computed as of decision bar T is a pure function of
// bars closed at or before T: appending, mutating, or replacing bars after T
// must not change the result. This is the RULE 4 (no lookahead) evidence.

#include "TestHarness.h"

#include "analysis/features/AnalyticalFeatureEngine.h"

#include <algorithm>
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

bool sameCross(const CrossTimeframeFeatures& a, const CrossTimeframeFeatures& b) {
    return a.asOfBarOpenSec == b.asOfBarOpenSec &&
           a.h4M15Agreement == b.h4M15Agreement &&
           a.h4D1Agreement == b.h4D1Agreement &&
           a.mtfConflictScore == b.mtfConflictScore &&
           a.h4StructuralAuthority == b.h4StructuralAuthority &&
           a.m15TriggerState == b.m15TriggerState &&
           a.m15Available == b.m15Available && a.h4Available == b.h4Available &&
           a.d1Available == b.d1Available && a.valid == b.valid &&
           a.quality == b.quality;
}

// Truncate every stream to the common decision instant's prefix; the cross
// features must be identical. This is the equivalence T13 (integration) relies
// on: "as-of the instant" == "computed over data up to that instant".
std::map<Timeframe, std::vector<Bar>> truncateAt(
    const std::map<Timeframe, std::vector<Bar>>& src, std::int64_t instant) {
    std::map<Timeframe, std::vector<Bar>> out;
    for (const auto& e : src) {
        std::vector<Bar> kept;
        for (const Bar& b : e.second) {
            if (b.openTimeSec <= instant) kept.push_back(b);
        }
        out[e.first] = kept;
    }
    return out;
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

// L1 fix: computeCross must not advance to the future when M15/H4 bars are
// appended. Pinned to a decision instant, future bars are irrelevant.
TEST_CASE(cross_future_m15_h4_bars_do_not_leak) {
    const AnalyticalFeatureEngine engine;
    const std::int64_t asOf = 29 * (intervalMillis(Timeframe::M15) / 1000);

    std::map<Timeframe, std::vector<Bar>> all;
    all[Timeframe::M15] = series(Timeframe::M15, 40, 1900.0, 0.6);
    all[Timeframe::H4] = series(Timeframe::H4, 40, 1900.0, 1.9);
    all[Timeframe::D1] = series(Timeframe::D1, 40, 1900.0, 4.0);

    const CrossTimeframeFeatures before = engine.computeCross(all, asOf);

    // Append wild FUTURE M15 and H4 bars: this is the exact probe that caught
    // the original defect (m15TriggerState/asOf moved). With a pinned asOf it
    // must be a no-op.
    const std::int64_t m15iv = intervalMillis(Timeframe::M15) / 1000;
    const std::int64_t h4iv = intervalMillis(Timeframe::H4) / 1000;
    for (std::size_t i = 40; i < 60; ++i) {
        const double mbase = 5000.0 + 100.0 * static_cast<double>(i);
        all[Timeframe::M15].push_back(test::makeBar(
            Timeframe::M15, static_cast<std::int64_t>(i) * m15iv, mbase,
            mbase + 500.0, mbase - 500.0, mbase + 400.0));
        const double hbase = 8000.0 + 200.0 * static_cast<double>(i);
        all[Timeframe::H4].push_back(test::makeBar(
            Timeframe::H4, static_cast<std::int64_t>(i) * h4iv, hbase,
            hbase + 900.0, hbase - 900.0, hbase + 700.0));
    }

    const CrossTimeframeFeatures after = engine.computeCross(all, asOf);

    CHECK_EQ(before.asOfBarOpenSec, after.asOfBarOpenSec);
    CHECK_EQ(before.m15TriggerState, after.m15TriggerState);
    CHECK_EQ(before.h4StructuralAuthority, after.h4StructuralAuthority);
    CHECK_EQ(before.h4M15Agreement, after.h4M15Agreement);
    CHECK_EQ(before.h4D1Agreement, after.h4D1Agreement);
    CHECK_EQ(before.mtfConflictScore, after.mtfConflictScore);
}

// L1 fix: with no explicit asOf, computeCross pins to the latest observed bar
// across the streams (a real closed bar), never a per-stream tail.
TEST_CASE(cross_default_asof_is_causal_common_instant) {
    const AnalyticalFeatureEngine engine;
    std::map<Timeframe, std::vector<Bar>> all;
    all[Timeframe::M15] = series(Timeframe::M15, 40, 1900.0, 0.6);
    all[Timeframe::H4] = series(Timeframe::H4, 40, 1900.0, 1.9);

    const CrossTimeframeFeatures before = engine.computeCross(all);
    const std::int64_t expected =
        std::max(all[Timeframe::M15].back().openTimeSec,
                 all[Timeframe::H4].back().openTimeSec);
    CHECK_EQ(before.asOfBarOpenSec, expected);

    // Appending FUTURE bars moves the default instant forward (that is a new
    // decision moment), but recomputing at the OLD instant is unchanged.
    const std::int64_t oldInstant = before.asOfBarOpenSec;
    const std::int64_t m15iv = intervalMillis(Timeframe::M15) / 1000;
    for (std::size_t j = 0; j < 15; ++j) {
        const std::int64_t open =
            oldInstant + static_cast<std::int64_t>(j + 1) * m15iv;
        const double base = 5000.0 + 100.0 * static_cast<double>(j);
        all[Timeframe::M15].push_back(
            test::makeBar(Timeframe::M15, open, base, base + 500.0,
                          base - 500.0, base + 400.0));
    }
    const CrossTimeframeFeatures pinned =
        engine.computeCross(all, oldInstant);
    CHECK_EQ(pinned.m15TriggerState, before.m15TriggerState);
    CHECK_EQ(pinned.asOfBarOpenSec, oldInstant);
}

// L2 fix: all nine per-timeframe vectors share one common decision instant.
TEST_CASE(compute_all_shares_one_decision_instant) {
    const AnalyticalFeatureEngine engine;
    std::map<Timeframe, std::vector<Bar>> all;
    // Deliberately unequal lengths (as in the audit probe).
    const std::size_t lens[9] = {40, 41, 42, 43, 44, 45, 46, 47, 48};
    std::size_t i = 0;
    for (Timeframe tf : allTimeframes()) {
        all[tf] = series(tf, lens[i++], 1900.0, 0.6);
    }

    const AnalyticalFeatureSet set = engine.computeAll(all);
    CHECK(set.valid);
    // Every per-timeframe vector reports the set's common instant.
    for (const auto& f : set.perTimeframe) {
        CHECK_EQ(f.asOfBarOpenSec, set.asOfBarOpenSec);
    }
    CHECK_EQ(set.cross.asOfBarOpenSec, set.asOfBarOpenSec);

    // Appending future bars to one stream must not change the set recomputed
    // at the old instant.
    const std::int64_t oldInstant = set.asOfBarOpenSec;
    const std::int64_t iv = intervalMillis(Timeframe::M15) / 1000;
    for (std::size_t j = 0; j < 20; ++j) {
        const std::int64_t open =
            oldInstant + static_cast<std::int64_t>(j + 1) * iv;
        const double base = 5000.0 + 100.0 * static_cast<double>(j);
        all[Timeframe::M15].push_back(
            test::makeBar(Timeframe::M15, open, base, base + 500.0,
                          base - 500.0, base + 400.0));
    }
    const AnalyticalFeatureSet after =
        engine.computeAll(all, oldInstant);
    CHECK_EQ(after.asOfBarOpenSec, oldInstant);
    CHECK_EQ(after.cross.m15TriggerState, set.cross.m15TriggerState);
}

// T13-relevant: an interior instant on unequal-length streams. Pinning is
// exactly equivalent to truncating every stream to that instant's prefix.
TEST_CASE(interior_instant_equals_truncated_prefix_across_streams) {
    const AnalyticalFeatureEngine engine;
    std::map<Timeframe, std::vector<Bar>> all;
    all[Timeframe::M15] = series(Timeframe::M15, 48, 1900.0, 0.6);
    all[Timeframe::H4] = series(Timeframe::H4, 44, 1900.0, 1.9);
    all[Timeframe::D1] = series(Timeframe::D1, 40, 1900.0, 5.0);

    // Choose an instant that is interior for every stream (H4 bar index 30).
    const std::int64_t instant = all[Timeframe::H4][30].openTimeSec;

    const AnalyticalFeatureSet pinned = engine.computeAll(all, instant);
    const AnalyticalFeatureSet truncated =
        engine.computeAll(truncateAt(all, instant), instant);

    CHECK_EQ(pinned.asOfBarOpenSec, instant);
    CHECK_EQ(truncated.asOfBarOpenSec, instant);
    CHECK(sameCross(pinned.cross, truncated.cross));
    for (std::size_t i = 0; i < pinned.perTimeframe.size(); ++i) {
        CHECK(sameFeatures(pinned.perTimeframe[i], truncated.perTimeframe[i]));
    }
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
