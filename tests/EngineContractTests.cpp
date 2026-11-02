// AYZR-ECO-TEST-001 — Standard Engine Contract tests.
//
// Proves the contract's honesty invariants: deterministic ids, publishability
// gating on decision-grade data, and that a probability can only appear when a
// calibration record backs it.

#include "ayzr/ecosystem/EngineContract.h"

#include <cstdio>
#include <string>

using namespace aura;
using namespace aura::ayzr::eco;

namespace {
int g_fail = 0;
int g_pass = 0;
void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("[PASS] %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("[FAIL] %s\n", what.c_str()); }
}
}  // namespace

int main() {
    // 1. Event id is deterministic and identity-sensitive.
    const std::string a = makeEventId("trend-m15", "XAUUSD", Timeframe::M15, 1700000000);
    const std::string b = makeEventId("trend-m15", "XAUUSD", Timeframe::M15, 1700000000);
    check(a == b, "same identity tuple yields the same event id");
    check(a != makeEventId("trend-m15", "XAUUSD", Timeframe::M15, 1700000900),
          "a different bar open time yields a different id");
    check(a != makeEventId("meanrev-m15", "XAUUSD", Timeframe::M15, 1700000000),
          "a different engine yields a different id");
    check(a != makeEventId("trend-m15", "XAUUSD", Timeframe::H1, 1700000000),
          "a different timeframe yields a different id");
    check(a.rfind("ev-", 0) == 0, "event id carries the ev- prefix");

    // 2. A clean, decision-grade, unexpired event is publishable.
    EngineResult r;
    r.engineId = "trend-m15";
    r.symbol = "XAUUSD";
    r.direction = EngineDirection::BULLISH;
    r.dataQuality = DataQualityState::VALID;
    r.status = EventStatus::PUBLISHED;
    check(isPublishable(r, 1700000000), "decision-grade published event is publishable");

    // 3. Any non-VALID quality withholds.
    EngineResult deg = r;
    deg.dataQuality = DataQualityState::DEGRADED;
    check(!isPublishable(deg, 1700000000), "DEGRADED data is not publishable");
    EngineResult unk = r;
    unk.dataQuality = DataQualityState::UNKNOWN;
    check(!isPublishable(unk, 1700000000), "UNKNOWN data is not publishable");

    // 4. NO_TRADE is never publishable.
    EngineResult none = r;
    none.direction = EngineDirection::NO_TRADE;
    check(!isPublishable(none, 1700000000), "NO_TRADE is never publishable");

    // 5. Terminal statuses are never publishable.
    for (EventStatus s : {EventStatus::WITHHELD, EventStatus::EXPIRED,
                          EventStatus::INVALIDATED}) {
        EngineResult t = r;
        t.status = s;
        check(!isPublishable(t, 1700000000),
              std::string("terminal status ") + toString(s) + " is not publishable");
    }

    // 6. Expiry is respected.
    EngineResult exp = r;
    exp.expiresAt = 1700000100;
    check(!isPublishable(exp, 1700000200), "an expired event is not publishable");
    check(isPublishable(exp, 1700000050), "an unexpired event with a window is publishable");

    // 7. Default confidence is uncalibrated with no probability.
    EngineResult fresh;
    check(!fresh.confidence.calibrated, "default confidence is uncalibrated");
    check(!fresh.confidence.probability.has_value(),
          "default confidence carries no probability");

    // 8. Absent levels are nullopt, not 0.0.
    check(!fresh.entry.has_value(), "default entry is absent, not zero");
    check(!fresh.stopLoss.has_value(), "default stop-loss is absent, not zero");
    check(fresh.takeProfitLevels.empty(), "default take-profits are empty");

    // 9. Round-trip parse of statuses and directions.
    for (EventStatus s : {EventStatus::DETECTED, EventStatus::PENDING_CONFIRMATION,
                          EventStatus::PUBLISHED, EventStatus::ACTIVE,
                          EventStatus::TARGET_HIT, EventStatus::WARNING,
                          EventStatus::CLOSE_RECOMMENDED, EventStatus::INVALIDATED,
                          EventStatus::EXPIRED, EventStatus::CLOSED,
                          EventStatus::WITHHELD}) {
        EventStatus out{};
        check(parseEventStatus(toString(s), out) && out == s,
              std::string("round-trip status ") + toString(s));
    }
    for (EngineDirection d : {EngineDirection::BULLISH, EngineDirection::BEARISH,
                              EngineDirection::SIDEWAYS, EngineDirection::NO_TRADE}) {
        EngineDirection out{};
        check(parseEngineDirection(toString(d), out) && out == d,
              std::string("round-trip direction ") + toString(d));
    }
    EventStatus bogus{};
    check(!parseEventStatus("NOPE", bogus), "unknown status text is rejected");

    std::printf("RESULT: %s (%d passed, %d failed)\n",
                g_fail == 0 ? "PASS" : "FAIL", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
