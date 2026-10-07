// Agent-A (T14) - explicit bounds + NaN/inf guards for every feature.
//
// Every field in AnalyticalFeatures.h declares a range. These tests assert,
// for deterministic edge cases and a fixed-seed pseudo-random walk, that:
//   * no field is ever NaN or +/-inf, and
//   * every field stays inside its documented range.
// A violated bound is an interpretability/honesty defect (a consumer that
// trusts the range would be misled), so it is a hard failure here.

#include "TestHarness.h"

#include "analysis/features/AnalyticalFeatureEngine.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

using namespace aura;

namespace {

bool inSigned(double v) { return std::isfinite(v) && v >= -1.0 - 1e-9 && v <= 1.0 + 1e-9; }
bool inUnit(double v)   { return std::isfinite(v) && v >= -1e-9 && v <= 1.0 + 1e-9; }
bool inTernary(double v){ return std::isfinite(v) && (v == -1.0 || v == 0.0 || v == 1.0); }

void checkTimeframeBounds(const TimeframeFeatures& f) {
    // signed [-1,1]
    CHECK(inSigned(f.structureTrend));
    CHECK(inSigned(f.swingAsymmetry));
    CHECK(inSigned(f.runBalance));
    CHECK(inSigned(f.momentumNorm));
    CHECK(inSigned(f.momentumAcceleration));
    CHECK(inSigned(f.netChangeRatio));
    CHECK(inSigned(f.patternScore));
    CHECK(inSigned(f.contextTrend));
    // unit [0,1]
    CHECK(inUnit(f.rangePosition));
    CHECK(inUnit(f.bodyRatio));
    CHECK(inUnit(f.upperWickRatio));
    CHECK(inUnit(f.lowerWickRatio));
    CHECK(inUnit(f.momentumPersistence));
    CHECK(inUnit(f.volatilityRatio));
    CHECK(inUnit(f.atrRatio));
    CHECK(inUnit(f.higherHighShare));
    CHECK(inUnit(f.lowerLowShare));
    CHECK(inUnit(f.contextVolatility));
    CHECK(inUnit(f.contextRangePosition));
    // ternary {-1,0,1}
    CHECK(inTernary(f.candleDirection));
}

void checkCrossBounds(const CrossTimeframeFeatures& c) {
    CHECK(inSigned(c.h4StructuralAuthority));
    CHECK(inSigned(c.m15TriggerState));
    CHECK(inTernary(c.h4M15Agreement));
    CHECK(inTernary(c.h4D1Agreement));
    CHECK(inUnit(c.mtfConflictScore));
}

// Deterministic LCG so the walk is reproducible without <random>.
struct Lcg {
    std::uint64_t s;
    explicit Lcg(std::uint64_t seed) : s(seed) {}
    double next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>((s >> 11) & 0x1FFFFFFFFFFFFFULL) /
               static_cast<double>(0x1FFFFFFFFFFFFFULL);
    }
};

std::vector<Bar> walk(Timeframe tf, std::size_t n, std::uint64_t seed,
                      double start, double vol) {
    Lcg rng(seed);
    std::vector<Bar> bars;
    const std::int64_t iv = intervalMillis(tf) / 1000;
    double price = start;
    for (std::size_t i = 0; i < n; ++i) {
        const double o = price;
        price += (rng.next() - 0.5) * 2.0 * vol;
        const double c = price;
        bars.push_back(test::makeBar(tf, static_cast<std::int64_t>(i) * iv, o,
                                     std::max(o, c) + vol * 0.5,
                                     std::min(o, c) - vol * 0.5, c));
    }
    return bars;
}

std::vector<Bar> ramp(Timeframe tf, std::size_t n, double start, double step) {
    std::vector<Bar> bars;
    const std::int64_t iv = intervalMillis(tf) / 1000;
    for (std::size_t i = 0; i < n; ++i) {
        const double o = start + step * static_cast<double>(i);
        const double c = o + step * 0.5;
        bars.push_back(test::makeBar(tf, static_cast<std::int64_t>(i) * iv, o,
                                     std::max(o, c) + 0.3,
                                     std::min(o, c) - 0.3, c));
    }
    return bars;
}

std::vector<Bar> flat(Timeframe tf, std::size_t n, double price) {
    std::vector<Bar> bars;
    const std::int64_t iv = intervalMillis(tf) / 1000;
    for (std::size_t i = 0; i < n; ++i) {
        bars.push_back(test::makeBar(tf, static_cast<std::int64_t>(i) * iv,
                                     price, price + 0.5, price - 0.5, price));
    }
    return bars;
}

}  // namespace

// Every feature, every timeframe, on a battery of deterministic edge cases.
TEST_CASE(t14_bounds_hold_on_edge_cases) {
    const AnalyticalFeatureEngine engine;
    for (Timeframe tf : allTimeframes()) {
        std::vector<std::vector<Bar>> cases = {
            walk(tf, 120, 0xABCDEFULL, 1900.0, 1.0),   // random walk
            ramp(tf, 120, 1900.0, 0.7),                // monotonic up
            ramp(tf, 120, 4000.0, -0.7),               // monotonic down
            flat(tf, 120, 2000.0),                     // flat
            walk(tf, 12, 0x1234ULL, 1900.0, 0.01),     // tiny move
            walk(tf, 120, 0x99ULL, 1e-3, 1e-3),        // near-zero prices
        };
        for (const auto& bars : cases) {
            const TimeframeFeatures f = engine.computeTimeframe(bars, tf);
            checkTimeframeBounds(f);
        }
    }
}

// A fixed-seed pseudo-random sweep: 9 timeframes x 40 seeds.
TEST_CASE(t14_bounds_hold_on_random_walk_sweep) {
    const AnalyticalFeatureEngine engine;
    std::uint64_t seed = 1;
    for (Timeframe tf : allTimeframes()) {
        for (int k = 0; k < 40; ++k) {
            const auto bars = walk(tf, 60 + (k % 90), seed++, 1500.0 + 10.0 * k, 2.0);
            checkTimeframeBounds(engine.computeTimeframe(bars, tf));
        }
    }
}

// Insufficient history must be rejected (never claimed valid) and finite.
TEST_CASE(t14_insufficient_history_is_rejected_and_finite) {
    const AnalyticalFeatureEngine engine;
    const std::vector<std::vector<Bar>> tooShort = {
        {},                                          // empty
        flat(Timeframe::M15, 1, 2000.0),             // single bar
        flat(Timeframe::M15, 2, 2000.0),             // two bars (< minTriggerBars)
    };
    for (const auto& bars : tooShort) {
        const TimeframeFeatures f = engine.computeTimeframe(bars, Timeframe::M15);
        checkTimeframeBounds(f);
        CHECK(!f.valid);
        CHECK(f.quality != DataQualityState::VALID);
    }
}

// Small-but-sufficient history and a flat (zero-direction) window are valid;
// they must still be finite and bounded.
TEST_CASE(t14_small_and_flat_windows_are_bounded) {
    const AnalyticalFeatureEngine engine;
    const std::vector<std::vector<Bar>> ok = {
        flat(Timeframe::M15, 60, 2000.0),           // flat, non-zero range
        walk(Timeframe::M15, 4, 7ULL, 2000.0, 0.5), // exactly minTriggerBars
    };
    for (const auto& bars : ok) {
        const TimeframeFeatures f = engine.computeTimeframe(bars, Timeframe::M15);
        checkTimeframeBounds(f);
        CHECK(f.valid);
    }
}

// A zero-range trigger window (open==high==low==close) is explicitly INVALID
// with a reason, and still bounded.
TEST_CASE(t14_zero_range_window_is_invalid_and_bounded) {
    const AnalyticalFeatureEngine engine;
    std::vector<Bar> bars;
    const std::int64_t iv = intervalMillis(Timeframe::H1) / 1000;
    for (std::size_t i = 0; i < 40; ++i) {
        bars.push_back(test::makeBar(Timeframe::H1, static_cast<std::int64_t>(i) * iv,
                                     2000.0, 2000.0, 2000.0, 2000.0));
    }
    const TimeframeFeatures f = engine.computeTimeframe(bars, Timeframe::H1);
    CHECK(!f.valid);
    CHECK(f.quality == DataQualityState::INVALID);
    checkTimeframeBounds(f);
}

// A pathological gap (1e12) must clamp, not overflow to inf/NaN.
TEST_CASE(t14_extreme_gap_stays_finite) {
    const AnalyticalFeatureEngine engine;
    std::vector<Bar> bars = ramp(Timeframe::H4, 40, 2000.0, 0.5);
    bars.push_back(test::makeBar(Timeframe::H4, 40 * (intervalMillis(Timeframe::H4) / 1000),
                                 1e12, 1e12 + 1e12, 1e12 - 1e12, 1e12 + 5e11));
    const TimeframeFeatures f = engine.computeTimeframe(bars, Timeframe::H4);
    checkTimeframeBounds(f);
}

// Cross-timeframe features must obey their ranges too, including when streams
// are missing or degenerate.
TEST_CASE(t14_cross_bounds_hold) {
    const AnalyticalFeatureEngine engine;
    std::map<Timeframe, std::vector<Bar>> good;
    good[Timeframe::M15] = walk(Timeframe::M15, 80, 3ULL, 1900.0, 1.0);
    good[Timeframe::H4] = walk(Timeframe::H4, 80, 5ULL, 1900.0, 3.0);
    good[Timeframe::D1] = walk(Timeframe::D1, 80, 8ULL, 1900.0, 6.0);
    checkCrossBounds(engine.computeCross(good));

    std::map<Timeframe, std::vector<Bar>> noD1 = good;
    noD1.erase(Timeframe::D1);
    checkCrossBounds(engine.computeCross(noD1));

    std::map<Timeframe, std::vector<Bar>> onlyM15;
    onlyM15[Timeframe::M15] = good[Timeframe::M15];
    checkCrossBounds(engine.computeCross(onlyM15));

    checkCrossBounds(engine.computeCross({}));  // empty map

    std::map<Timeframe, std::vector<Bar>> degenerate = good;
    degenerate[Timeframe::M15] = flat(Timeframe::M15, 80, 2000.0);
    degenerate[Timeframe::H4] = flat(Timeframe::H4, 80, 2000.0);
    degenerate[Timeframe::D1] = {};
    checkCrossBounds(engine.computeCross(degenerate));
}

// computeAll: every per-timeframe vector and the cross block stay bounded,
// including absent streams.
TEST_CASE(t14_all_set_bounds_hold) {
    const AnalyticalFeatureEngine engine;
    std::map<Timeframe, std::vector<Bar>> streams;
    streams[Timeframe::M15] = walk(Timeframe::M15, 80, 11ULL, 1900.0, 1.0);
    streams[Timeframe::H4] = walk(Timeframe::H4, 80, 13ULL, 1900.0, 3.0);
    streams[Timeframe::D1] = walk(Timeframe::D1, 80, 17ULL, 1900.0, 6.0);

    const AnalyticalFeatureSet set = engine.computeAll(streams);
    CHECK_EQ(set.perTimeframe.size(), kTimeframeCount);
    for (const TimeframeFeatures& f : set.perTimeframe) {
        checkTimeframeBounds(f);
    }
    checkCrossBounds(set.cross);

    // A single-stream call: the other eight are UNKNOWN, still bounded.
    std::map<Timeframe, std::vector<Bar>> one;
    one[Timeframe::M15] = streams[Timeframe::M15];
    const AnalyticalFeatureSet sparse = engine.computeAll(one);
    for (const TimeframeFeatures& f : sparse.perTimeframe) {
        checkTimeframeBounds(f);
    }
    checkCrossBounds(sparse.cross);
}
