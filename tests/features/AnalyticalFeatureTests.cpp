// Agent-A (T01) - Analytical feature unit tests.
//
// Verifies: boundedness, interpretability (sign/shape), determinism, validity
// honesty, and cross-timeframe wiring. Causality/leakage is covered separately
// in AnalyticalFeatureLeakageTests.cpp.

#include "TestHarness.h"

#include "analysis/features/AnalyticalFeatureEngine.h"

#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace aura;

namespace {

constexpr std::int64_t kSec = 1;

std::vector<Bar> makeSeries(Timeframe tf, std::size_t n, double start,
                            double step) {
    std::vector<Bar> bars;
    const std::int64_t iv = intervalMillis(tf) / 1000;
    for (std::size_t i = 0; i < n; ++i) {
        const double o = start + step * static_cast<double>(i);
        const double c = o + step * 0.5;
        const double h = std::max(o, c) + 0.3;
        const double l = std::min(o, c) - 0.3;
        bars.push_back(test::makeBar(tf, static_cast<std::int64_t>(i) * iv, o, h,
                                     l, c));
    }
    return bars;
}

bool inUnit(double x) { return x >= 0.0 && x <= 1.0; }
bool inSigned(double x) { return x >= -1.0 && x <= 1.0; }

void checkBounded(const TimeframeFeatures& f) {
    CHECK(inUnit(f.bodyRatio));
    CHECK(inUnit(f.upperWickRatio));
    CHECK(inUnit(f.lowerWickRatio));
    CHECK(f.candleDirection >= -1.0 && f.candleDirection <= 1.0);
    CHECK(inUnit(f.runBalance == 0.0 ? 0.5 : std::fabs(f.runBalance)));
    CHECK(inSigned(f.structureTrend));
    CHECK(inUnit(f.rangePosition));
    CHECK(inSigned(f.swingAsymmetry));
    CHECK(inSigned(f.momentumNorm));
    CHECK(inUnit(f.momentumPersistence));
    CHECK(inSigned(f.momentumAcceleration));
    CHECK(inUnit(f.volatilityRatio));
    CHECK(inUnit(f.atrRatio));
    CHECK(inSigned(f.netChangeRatio));
    CHECK(inSigned(f.patternScore));
    CHECK(inSigned(f.contextTrend));
    CHECK(inUnit(f.contextVolatility));
    CHECK(inUnit(f.contextRangePosition));
    CHECK(f.windowUsed <= kTriggerWindow);
}

}  // namespace

TEST_CASE(features_are_bounded) {
    const AnalyticalFeatureEngine engine;
    for (Timeframe tf : allTimeframes()) {
        const auto bars = makeSeries(tf, 120, 1900.0, 0.7);
        const TimeframeFeatures f = engine.computeTimeframe(bars, tf);
        CHECK(f.valid);
        checkBounded(f);
    }
}

TEST_CASE(features_are_interpretable_for_trend) {
    const AnalyticalFeatureEngine engine;
    const auto up = makeSeries(Timeframe::M15, 120, 1800.0, 0.9);
    const auto down = makeSeries(Timeframe::M15, 120, 1800.0, -0.9);

    const TimeframeFeatures fu = engine.computeTimeframe(up, Timeframe::M15);
    const TimeframeFeatures fd = engine.computeTimeframe(down, Timeframe::M15);

    CHECK(fu.structureTrend > 0.0);
    CHECK(fd.structureTrend < 0.0);
    CHECK(fu.patternScore > 0.0);
    CHECK(fd.patternScore < 0.0);
    CHECK(fu.rangePosition > 0.5);   // rising closes near window high
    CHECK(fd.rangePosition < 0.5);   // falling closes near window low
    CHECK(fu.candleDirection > 0.0);
    CHECK(fd.candleDirection < 0.0);
    CHECK(fu.higherHighShare > 0.5);
    CHECK(fd.lowerLowShare > 0.5);
}

TEST_CASE(candle_ratios_partition_the_range) {
    const AnalyticalFeatureEngine engine;
    const auto bars = makeSeries(Timeframe::H1, 60, 1900.0, 0.5);
    const TimeframeFeatures f = engine.computeTimeframe(bars, Timeframe::H1);
    // body + upper wick + lower wick == full range, so the ratios sum to 1.
    CHECK(std::fabs((f.bodyRatio + f.upperWickRatio + f.lowerWickRatio) - 1.0) <
          1e-9);
}

TEST_CASE(features_are_deterministic) {
    const AnalyticalFeatureEngine engine;
    const auto bars = makeSeries(Timeframe::M30, 90, 1950.0, 0.33);
    const TimeframeFeatures a = engine.computeTimeframe(bars, Timeframe::M30);
    const TimeframeFeatures b = engine.computeTimeframe(bars, Timeframe::M30);
    CHECK_EQ(a.structureTrend, b.structureTrend);
    CHECK_EQ(a.momentumNorm, b.momentumNorm);
    CHECK_EQ(a.patternScore, b.patternScore);
    CHECK_EQ(a.contextTrend, b.contextTrend);
    CHECK_EQ(a.asOfBarOpenSec, b.asOfBarOpenSec);
}

TEST_CASE(insufficient_history_is_not_valid) {
    const AnalyticalFeatureEngine engine;
    const auto bars = makeSeries(Timeframe::M15, 2, 1900.0, 0.5);
    const TimeframeFeatures f = engine.computeTimeframe(bars, Timeframe::M15);
    CHECK(!f.valid);
    CHECK(f.quality == DataQualityState::INCOMPLETE);
    CHECK(!f.detail.empty());
}

TEST_CASE(empty_input_is_not_valid) {
    const AnalyticalFeatureEngine engine;
    const TimeframeFeatures f =
        engine.computeTimeframe({}, Timeframe::M15);
    CHECK(!f.valid);
    CHECK(f.quality == DataQualityState::INCOMPLETE);
}

TEST_CASE(cross_agreement_and_conflict) {
    const AnalyticalFeatureEngine engine;
    std::map<Timeframe, std::vector<Bar>> up;
    up[Timeframe::M15] = makeSeries(Timeframe::M15, 60, 1900.0, 0.7);
    up[Timeframe::H4] = makeSeries(Timeframe::H4, 60, 1900.0, 2.0);
    up[Timeframe::D1] = makeSeries(Timeframe::D1, 60, 1900.0, 5.0);
    const CrossTimeframeFeatures agree = engine.computeCross(up);
    CHECK(agree.valid);
    CHECK(agree.m15Available);
    CHECK(agree.h4Available);
    CHECK(agree.d1Available);
    CHECK_EQ(agree.h4M15Agreement, 1.0);
    CHECK_EQ(agree.h4D1Agreement, 1.0);
    CHECK_EQ(agree.mtfConflictScore, 0.0);
    CHECK(agree.h4StructuralAuthority > 0.0);

    std::map<Timeframe, std::vector<Bar>> mixed;
    mixed[Timeframe::M15] = makeSeries(Timeframe::M15, 60, 1900.0, 0.7);
    mixed[Timeframe::H4] = makeSeries(Timeframe::H4, 60, 1900.0, -2.0);
    const CrossTimeframeFeatures conflict = engine.computeCross(mixed);
    CHECK(conflict.valid);
    CHECK(!conflict.d1Available);
    CHECK_EQ(conflict.h4M15Agreement, 0.0);
    CHECK_EQ(conflict.mtfConflictScore, 1.0);   // only available pair disagrees
    CHECK(conflict.h4StructuralAuthority < 0.0);
}

TEST_CASE(cross_missing_core_stream_is_not_valid) {
    const AnalyticalFeatureEngine engine;
    std::map<Timeframe, std::vector<Bar>> onlyM15;
    onlyM15[Timeframe::M15] = makeSeries(Timeframe::M15, 60, 1900.0, 0.7);
    const CrossTimeframeFeatures c = engine.computeCross(onlyM15);
    CHECK(!c.valid);
    CHECK(c.m15Available);
    CHECK(!c.h4Available);
    CHECK(c.quality == DataQualityState::DEGRADED);
}

TEST_CASE(compute_all_covers_nine_streams) {
    const AnalyticalFeatureEngine engine;
    std::map<Timeframe, std::vector<Bar>> all;
    for (Timeframe tf : allTimeframes()) {
        all[tf] = makeSeries(tf, 80, 1900.0, 0.6);
    }
    const AnalyticalFeatureSet set = engine.computeAll(all);
    CHECK_EQ(set.perTimeframe.size(), kTimeframeCount);
    CHECK(set.valid);
    CHECK(set.quality == DataQualityState::VALID);
    for (const auto& f : set.perTimeframe) {
        CHECK(f.valid);
        checkBounded(f);
    }

    // Drop one stream: it must be UNKNOWN, not fabricated.
    all.erase(Timeframe::W1);
    const AnalyticalFeatureSet partial = engine.computeAll(all);
    CHECK(!partial.valid);
    CHECK_EQ(partial.perTimeframe.size(), kTimeframeCount);
    CHECK(partial.perTimeframe[7].timeframe == Timeframe::W1);
    CHECK(!partial.perTimeframe[7].valid);
    CHECK(partial.perTimeframe[7].quality == DataQualityState::UNKNOWN);
}
