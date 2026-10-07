// Agent-A (T02) - RULE A evidence for the feature layer.
//
// RULE A forbids a reward-structure artifact: an asymmetric target/stop (or any
// construction) that manufactures a flattering result. A feature layer has no
// target/stop, but it can still smuggle in a directional bias. The testable
// property is symmetry: mirroring the market (up <-> down) must not create a
// long or short preference.
//
// Mirror: reflect prices about a constant K, swapping high/low
//   open' = K - open, high' = K - low, low' = K - high, close' = K - close
// A direction-neutral engine then satisfies, exactly:
//   * sign features are antisymmetric:  x' == -x
//   * position features reflect about 0.5: x' == 1 - x
//   * wick/extreme features swap:        upperWick' == lowerWick, hh' == ll
//   * magnitude features are invariant:  x' == x
// Any deviation is a long/short bias (a RULE A artifact).

#include "TestHarness.h"

#include "analysis/features/AnalyticalFeatureEngine.h"

#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace aura;

namespace {

constexpr double kMirrorK = 4000.0;

Bar mirror(const Bar& b) {
    return test::makeBar(b.timeframe, b.openTimeSec, kMirrorK - b.open,
                         kMirrorK - b.low, kMirrorK - b.high,
                         kMirrorK - b.close);
}

std::vector<Bar> series(Timeframe tf, std::size_t n, double start, double step) {
    std::vector<Bar> bars;
    const std::int64_t iv = intervalMillis(tf) / 1000;
    for (std::size_t i = 0; i < n; ++i) {
        const double o = start + step * static_cast<double>(i);
        const double c = o + step * 0.4;
        bars.push_back(test::makeBar(tf, static_cast<std::int64_t>(i) * iv, o,
                                     std::max(o, c) + 0.25,
                                     std::min(o, c) - 0.25, c));
    }
    return bars;
}

std::vector<Bar> mirrored(const std::vector<Bar>& bars) {
    std::vector<Bar> out;
    out.reserve(bars.size());
    for (const Bar& b : bars) out.push_back(mirror(b));
    return out;
}

bool near(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

}  // namespace

// Sign features carry direction; they must flip sign and nothing else.
TEST_CASE(rule_a_sign_features_are_antisymmetric) {
    const AnalyticalFeatureEngine engine;
    for (Timeframe tf : allTimeframes()) {
        const auto up = series(tf, 120, 1900.0, 0.8);
        const auto dn = mirrored(up);
        const TimeframeFeatures a = engine.computeTimeframe(up, tf);
        const TimeframeFeatures b = engine.computeTimeframe(dn, tf);
        CHECK(a.valid && b.valid);

        CHECK(near(b.structureTrend, -a.structureTrend));
        CHECK(near(b.runBalance, -a.runBalance));
        CHECK(near(b.momentumNorm, -a.momentumNorm));
        CHECK(near(b.netChangeRatio, -a.netChangeRatio));
        CHECK(near(b.patternScore, -a.patternScore));
        CHECK(near(b.swingAsymmetry, -a.swingAsymmetry));
        CHECK(near(b.contextTrend, -a.contextTrend));
        CHECK(near(b.candleDirection, -a.candleDirection));
    }
}

// Position features are measured from the window low; a mirror flips them
// about the midpoint 0.5.
TEST_CASE(rule_a_position_features_reflect_about_half) {
    const AnalyticalFeatureEngine engine;
    const auto up = series(Timeframe::M15, 120, 1900.0, 0.8);
    const auto dn = mirrored(up);
    const TimeframeFeatures a = engine.computeTimeframe(up, Timeframe::M15);
    const TimeframeFeatures b = engine.computeTimeframe(dn, Timeframe::M15);
    CHECK(near(b.rangePosition, 1.0 - a.rangePosition));
    CHECK(near(b.contextRangePosition, 1.0 - a.contextRangePosition));
}

// Wick and high/low-count features are mirror images of each other.
TEST_CASE(rule_a_extreme_features_swap) {
    const AnalyticalFeatureEngine engine;
    const auto up = series(Timeframe::H1, 120, 1900.0, 0.8);
    const auto dn = mirrored(up);
    const TimeframeFeatures a = engine.computeTimeframe(up, Timeframe::H1);
    const TimeframeFeatures b = engine.computeTimeframe(dn, Timeframe::H1);
    CHECK(near(b.upperWickRatio, a.lowerWickRatio));
    CHECK(near(b.lowerWickRatio, a.upperWickRatio));
    CHECK(near(b.higherHighShare, a.lowerLowShare));
    CHECK(near(b.lowerLowShare, a.higherHighShare));
}

// Magnitude features have no direction and must be unchanged by a mirror.
TEST_CASE(rule_a_magnitude_features_are_invariant) {
    const AnalyticalFeatureEngine engine;
    for (Timeframe tf : allTimeframes()) {
        const auto up = series(tf, 120, 1900.0, 0.8);
        const auto dn = mirrored(up);
        const TimeframeFeatures a = engine.computeTimeframe(up, tf);
        const TimeframeFeatures b = engine.computeTimeframe(dn, tf);
        CHECK(near(b.bodyRatio, a.bodyRatio));
        CHECK(near(b.momentumPersistence, a.momentumPersistence));
        CHECK(near(b.momentumAcceleration, a.momentumAcceleration));
        CHECK(near(b.atrRatio, a.atrRatio));
    }
}

// Volatility regime under a mirror. ATR-ratio is exactly direction-neutral.
// The log-return share is direction-neutral too, but only to second order:
// reflection maps a log-return r to log(1-r), not -log(1+r), so |r| is not
// preserved. That is a bounded magnitude asymmetry (no long/short preference),
// and it is documented here rather than hidden.
TEST_CASE(rule_a_volatility_is_direction_neutral) {
    const AnalyticalFeatureEngine engine;
    const double r = 0.002;
    std::vector<Bar> bars;
    double price = 2000.0;
    const std::int64_t iv = intervalMillis(Timeframe::M15) / 1000;
    for (std::size_t i = 0; i < 120; ++i) {
        const double o = price;
        price *= std::exp((i % 2 == 0) ? r : -r);
        const double c = price;
        bars.push_back(test::makeBar(Timeframe::M15,
                                     static_cast<std::int64_t>(i) * iv, o,
                                     std::max(o, c) + 0.1,
                                     std::min(o, c) - 0.1, c));
    }
    const TimeframeFeatures f = engine.computeTimeframe(bars, Timeframe::M15);
    CHECK(f.valid);
    CHECK(f.volatilityRatio > 0.0 && f.volatilityRatio < 1.0);
    CHECK(f.atrRatio > 0.0 && f.atrRatio < 1.0);

    const TimeframeFeatures m =
        engine.computeTimeframe(mirrored(bars), Timeframe::M15);
    CHECK(near(m.atrRatio, f.atrRatio));            // exact
    CHECK(near(m.volatilityRatio, f.volatilityRatio, 1e-2));  // second-order
}

// The cross-timeframe block must have no directional bias either: mirroring
// every stream flips the authority/trigger signs and the agreement signs.
TEST_CASE(rule_a_cross_features_are_antisymmetric) {
    const AnalyticalFeatureEngine engine;
    std::map<Timeframe, std::vector<Bar>> up;
    up[Timeframe::M15] = series(Timeframe::M15, 80, 1900.0, 0.7);
    up[Timeframe::H4] = series(Timeframe::H4, 80, 1900.0, 2.0);
    up[Timeframe::D1] = series(Timeframe::D1, 80, 1900.0, 5.0);

    std::map<Timeframe, std::vector<Bar>> dn;
    for (const auto& e : up) dn[e.first] = mirrored(e.second);

    const CrossTimeframeFeatures a = engine.computeCross(up);
    const CrossTimeframeFeatures b = engine.computeCross(dn);
    CHECK(a.valid && b.valid);
    CHECK(near(b.h4StructuralAuthority, -a.h4StructuralAuthority));
    CHECK(near(b.m15TriggerState, -a.m15TriggerState));
    CHECK(near(b.h4M15Agreement, -a.h4M15Agreement));
    CHECK(near(b.h4D1Agreement, -a.h4D1Agreement));
    CHECK(near(b.mtfConflictScore, a.mtfConflictScore));
}

// A perfectly flat market has no direction to bias toward: all sign features
// are exactly zero.
TEST_CASE(rule_a_flat_market_has_no_direction) {
    const AnalyticalFeatureEngine engine;
    std::vector<Bar> flat;
    const std::int64_t iv = intervalMillis(Timeframe::H1) / 1000;
    for (std::size_t i = 0; i < 60; ++i) {
        flat.push_back(test::makeBar(Timeframe::H1,
                                     static_cast<std::int64_t>(i) * iv, 2000.0,
                                     2000.5, 1999.5, 2000.0));
    }
    const TimeframeFeatures f = engine.computeTimeframe(flat, Timeframe::H1);
    CHECK(f.valid);
    CHECK_EQ(f.structureTrend, 0.0);
    CHECK_EQ(f.runBalance, 0.0);
    CHECK_EQ(f.momentumNorm, 0.0);
    CHECK_EQ(f.netChangeRatio, 0.0);
    CHECK_EQ(f.candleDirection, 0.0);
    CHECK_EQ(f.patternScore, 0.0);
}
