#pragma once
// Minimal deterministic test harness.
//
// Each tests/*.cpp is compiled into its own executable by the build graph, so
// this header supplies both the registration machinery and main(). A test that
// fails any CHECK returns a non-zero exit code, which CTest reports.

#include "data/BarNormalizer.h"
#include "foundation/DataQualityState.h"
#include "foundation/Timestamp.h"
#include "mt5/Mt5BridgeContract.h"

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace aura {
namespace test {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& cases() {
    static std::vector<Case> instance;
    return instance;
}

inline int& failures() {
    static int instance = 0;
    return instance;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        cases().push_back({name, std::move(fn)});
    }
};

// A closed, validated bar at `openSec` for the given timeframe.
inline Bar makeBar(Timeframe timeframe, std::int64_t openSec, double open,
                   double high, double low, double close,
                   DataQualityState quality = DataQualityState::VALID) {
    Bar bar;
    bar.timeframe = timeframe;
    bar.openTimeSec = openSec;
    bar.open = open;
    bar.high = high;
    bar.low = low;
    bar.close = close;
    bar.tickVolume = 100;
    bar.realVolume = 100;
    bar.spread = 5;
    bar.receivedAt = Timestamp::fromEpochMillis(openSec * 1000);
    bar.quality = quality;
    bar.sourceSymbol = "XAUUSD";
    bar.sourceBroker = "test-broker";
    return bar;
}

}  // namespace test
}  // namespace aura

#define TEST_CASE(name)                                                     \
    static void name();                                                     \
    static ::aura::test::Registrar aura_registrar_##name(#name, name);      \
    static void name()

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            std::cerr << "  CHECK failed: " #condition " at " << __FILE__   \
                      << ":" << __LINE__ << "\n";                           \
            ++::aura::test::failures();                                     \
        }                                                                   \
    } while (0)

#define CHECK_EQ(actual, expected)                                          \
    do {                                                                    \
        const auto aura_a = (actual);                                       \
        const auto aura_e = (expected);                                     \
        if (!(aura_a == aura_e)) {                                          \
            std::cerr << "  CHECK_EQ failed: " #actual " != " #expected     \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n";     \
            ++::aura::test::failures();                                     \
        }                                                                   \
    } while (0)

#define CHECK_TRUE(condition) CHECK(condition)

#define REQUIRE(condition)                                                  \
    do {                                                                    \
        if (!(condition)) {                                                 \
            std::cerr << "  REQUIRE failed: " #condition " at " << __FILE__ \
                      << ":" << __LINE__ << "\n";                           \
            ++::aura::test::failures();                                     \
            return;                                                         \
        }                                                                   \
    } while (0)

int main() {
    int executed = 0;
    int failedCases = 0;
    for (const auto& testCase : aura::test::cases()) {
        ++executed;
        const int before = aura::test::failures();
        testCase.fn();
        const bool ok = aura::test::failures() == before;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << testCase.name << "\n";
        if (!ok) ++failedCases;
    }
    std::cout << executed << " test case(s), " << failedCases << " failed, "
              << aura::test::failures() << " assertion failure(s)\n";
    if (failedCases != 0) {
        std::cout << "RESULT: FAIL\n";
        return 1;
    }
    std::cout << "RESULT: PASS\n";
    return 0;
}
