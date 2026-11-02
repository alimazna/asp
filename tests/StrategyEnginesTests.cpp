// AYZR-ENG-TEST-102 — Five specialised strategy engines.
//
// Each engine is tested for BOTH its positive case (a crafted, realistic window
// that meets its conditions) and its refusal cases (the situations the spec says
// it must NOT trade). Deterministic inputs; no mocks of the engine itself.

#include "ayzr/engines/BreakoutEngine.h"
#include "ayzr/engines/EventDrivenEngine.h"
#include "ayzr/engines/MeanReversionEngine.h"
#include "ayzr/engines/MicrostructureEngine.h"
#include "ayzr/engines/TrendFollowingEngine.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace aura;
using namespace aura::ayzr::eco;

namespace {
int g_fail = 0;
int g_pass = 0;
void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("[PASS] %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("[FAIL] %s\n", what.c_str()); }
}

constexpr std::int64_t kStep = 900;   // M15
constexpr std::int64_t kT0 = 1700000000;

/// Build a view from a close series; OHLC derived so high/low bracket close.
MarketView viewFromCloses(const std::vector<double>& closes, double wick = 0.5) {
    MarketView v;
    v.symbol = "XAUUSD";
    v.timeframe = Timeframe::M15;
    v.dataSource = "test";
    v.quality = DataQualityState::VALID;
    v.snapshotUtc = kT0 + static_cast<std::int64_t>(closes.size()) * kStep;
    double prev = closes.front();
    for (std::size_t i = 0; i < closes.size(); ++i) {
        Bar b;
        b.openTimeUtc = kT0 + static_cast<std::int64_t>(i) * kStep;
        b.open = prev;
        b.close = closes[i];
        b.high = std::max(b.open, b.close) + wick;
        b.low = std::min(b.open, b.close) - wick;
        b.tickVolume = 100;
        v.bars.push_back(b);
        prev = closes[i];
    }
    v.latestOpenUtc = v.bars.back().openTimeUtc;
    return v;
}
}  // namespace

int main() {
    // ===================== TREND FOLLOWING ==================================
    {
        TrendFollowingEngine eng;
        // Steady uptrend: EMA fast > slow > trend, rising, RSI high.
        std::vector<double> up;
        for (int i = 0; i < 80; ++i) up.push_back(1900.0 + i * 1.5);
        auto r = eng.evaluate(viewFromCloses(up));
        check(!r.empty(), "trend: emits a result on a clean uptrend");
        if (!r.empty()) {
            check(r[0].direction == EngineDirection::BULLISH, "trend: uptrend => BULLISH");
            check(r[0].stopLoss.has_value() && r[0].stopLoss->price < r[0].entry->price,
                  "trend: long stop-loss is below entry");
            check(r[0].takeProfitLevels.size() == 2, "trend: two take-profit levels");
            check(!r[0].confidence.probability.has_value(),
                  "trend: no probability without calibration");
            check(r[0].status == EventStatus::PENDING_CONFIRMATION,
                  "trend: never auto-publishes");
        }
        // Downtrend.
        std::vector<double> dn;
        for (int i = 0; i < 80; ++i) dn.push_back(2100.0 - i * 1.5);
        auto r2 = eng.evaluate(viewFromCloses(dn));
        check(!r2.empty() && r2[0].direction == EngineDirection::BEARISH,
              "trend: downtrend => BEARISH");
        // Flat market: must NOT emit a direction.
        std::vector<double> flat(80, 2000.0);
        for (std::size_t i = 0; i < flat.size(); ++i)
            flat[i] += (i % 2 == 0) ? 0.3 : -0.3;
        auto r3 = eng.evaluate(viewFromCloses(flat));
        check(!r3.empty() && r3[0].direction == EngineDirection::NO_TRADE,
              "trend: a flat market is NO_TRADE, not a guess");
        // Too little history: silent.
        std::vector<double> tiny(5, 2000.0);
        check(eng.evaluate(viewFromCloses(tiny)).empty(),
              "trend: insufficient history => no result");
    }

    // ===================== MEAN REVERSION ===================================
    {
        MeanReversionEngine eng;
        // Range-bound series with one sharp down-spike at the end.
        std::vector<double> range;
        for (int i = 0; i < 60; ++i) range.push_back(2000.0 + ((i % 4) - 1.5) * 1.0);
        range.push_back(1985.0);   // big downward stretch
        auto r = eng.evaluate(viewFromCloses(range));
        check(!r.empty(), "meanrev: emits a result on a stretched range");
        if (!r.empty()) {
            check(r[0].direction == EngineDirection::BULLISH,
                  "meanrev: downward stretch => BULLISH reversion");
            check(r[0].status == EventStatus::PENDING_CONFIRMATION,
                  "meanrev: pending confirmation, not published");
        }
        // Strong trend: must refuse.
        std::vector<double> trend;
        for (int i = 0; i < 60; ++i) trend.push_back(1900.0 + i * 3.0);
        auto r2 = eng.evaluate(viewFromCloses(trend));
        check(!r2.empty() && r2[0].direction == EngineDirection::NO_TRADE,
              "meanrev: refuses to fade a strong trend");
        check(!r2.empty() && r2[0].status == EventStatus::WITHHELD,
              "meanrev: trend refusal is WITHHELD");
        // No dispersion (flat): must not trade.
        std::vector<double> dead(60, 2000.0);
        auto r3 = eng.evaluate(viewFromCloses(dead));
        check(r3.empty() || r3[0].direction == EngineDirection::NO_TRADE,
              "meanrev: zero dispersion => no trade");
    }

    // ===================== BREAKOUT =========================================
    {
        BreakoutEngine eng;
        // Tight range then a clean upside break on the last bar.
        std::vector<double> box;
        for (int i = 0; i < 30; ++i) box.push_back(2000.0 + ((i % 3) - 1) * 0.6);
        box.push_back(2010.0);   // decisive break above the box
        auto r = eng.evaluate(viewFromCloses(box, 0.3));
        check(!r.empty(), "breakout: emits a result");
        if (!r.empty()) {
            check(r[0].direction == EngineDirection::BULLISH,
                  "breakout: upside break => BULLISH");
            check(r[0].status == EventStatus::PENDING_CONFIRMATION,
                  "breakout: session activates monitoring, never auto-fires");
        }
        // No break: inside the box.
        std::vector<double> noBox;
        for (int i = 0; i < 30; ++i) noBox.push_back(2000.0 + ((i % 3) - 1) * 0.6);
        auto r2 = eng.evaluate(viewFromCloses(noBox, 0.3));
        check(!r2.empty() && r2[0].direction == EngineDirection::NO_TRADE,
              "breakout: no break => NO_TRADE");
        // Break out of a wide (non-compressed) range: refused. A slow drift
        // builds a tall box while each bar's ATR stays small, so height/ATR is
        // large — a break of THAT is a chase, not an explosive breakout.
        std::vector<double> wide;
        for (int i = 0; i < 30; ++i) wide.push_back(2000.0 + i * 2.0);
        wide.push_back(2080.0);
        auto r3 = eng.evaluate(viewFromCloses(wide, 0.3));
        check(!r3.empty() && r3[0].direction == EngineDirection::NO_TRADE,
              "breakout: a break of a wide range is refused (chase guard)");
    }

    // ===================== MICROSTRUCTURE ===================================
    {
        MicrostructureEngine eng;
        std::vector<double> c(60, 2000.0);
        auto v = viewFromCloses(c);
        auto r = eng.evaluate(v);
        check(!r.empty(), "micro: emits an honest status");
        if (!r.empty()) {
            check(r[0].direction == EngineDirection::NO_TRADE,
                  "micro: never fabricates a direction");
            check(r[0].status == EventStatus::WARNING, "micro: reports WARNING");
            check(r[0].reasonCodes.size() == 1 &&
                      r[0].reasonCodes[0] == "microstructure_unavailable",
                  "micro: explicitly says microstructure is unavailable");
        }
        check(eng.health(v, v.snapshotUtc) == EngineHealth::UNAVAILABLE,
              "micro: health is UNAVAILABLE without a feed");
        // With a real feed, it observes spread but still does not trade.
        MicrostructureInput mi;
        mi.bid = 1999.5; mi.ask = 2000.5; mi.sourceId = "verified_feed";
        auto r2 = eng.evaluateWith(v, mi);
        check(!r2.empty() && r2[0].direction == EngineDirection::NO_TRADE,
              "micro: even with a feed, no direction is fabricated");
        check(!r2.empty() && !r2[0].supportingEvidence.empty(),
              "micro: reports the observed spread as evidence");
    }

    // ===================== EVENT DRIVEN =====================================
    {
        EventDrivenEngine eng;   // default: ruleSetTested = false
        std::vector<double> c(60, 2000.0);
        for (std::size_t i = 0; i < c.size(); ++i) c[i] += (i % 2) ? 0.5 : -0.5;
        auto v = viewFromCloses(c);
        const std::int64_t now = v.snapshotUtc;

        EconomicEvent ev;
        ev.eventId = "cpi-2024-01";
        ev.title = "US CPI";
        ev.sourceId = "bls";
        ev.verified = true;
        ev.publishedAt = now - 600;
        ev.goldBias = 0.6;
        ev.summaryAr = "تضخم أعلى من المتوقع";
        ev.actual = 3.4; ev.forecast = 3.1;

        // Condition 5 unmet (ruleset not tested): warning, not a trade.
        auto r = eng.evaluateEvent(v, ev, now);
        check(!r.empty() && r[0].direction == EngineDirection::NO_TRADE,
              "event: untested ruleset => no trade");
        check(!r.empty() && r[0].status == EventStatus::WARNING,
              "event: untested ruleset => WARNING");

        // Now mark the ruleset tested: all five conditions can be met.
        EventDrivenConfig cfg;
        cfg.ruleSetTested = true;
        EventDrivenEngine eng2(Timeframe::M15, cfg);
        auto r2 = eng2.evaluateEvent(v, ev, now);
        check(!r2.empty() && r2[0].direction == EngineDirection::BULLISH,
              "event: verified+recent+fresh+tested => BULLISH");
        check(!r2.empty() && r2[0].entry.has_value() && r2[0].stopLoss.has_value() &&
                  !r2[0].takeProfitLevels.empty(),
              "event: explainable entry/SL/TP present");
        check(!r2.empty() && r2[0].status == EventStatus::PENDING_CONFIRMATION,
              "event: still pending confirmation, never auto-published");

        // Unverified rumour: warning only.
        EconomicEvent rum = ev;
        rum.verified = false;
        auto r3 = eng2.evaluateEvent(v, rum, now);
        check(!r3.empty() && r3[0].direction == EngineDirection::NO_TRADE,
              "event: unverified rumour => no trade");
        check(!r3.empty() && !r3[0].riskFlags.empty() &&
                  r3[0].riskFlags[0] == "rumour",
              "event: rumour is flagged");

        // Stale news: warning only.
        EconomicEvent old = ev;
        old.publishedAt = now - 7 * 3600;
        auto r4 = eng2.evaluateEvent(v, old, now);
        check(!r4.empty() && r4[0].direction == EngineDirection::NO_TRADE,
              "event: stale news => no trade");

        // Fresh event but stale candles: warning only.
        MarketView staleView = v;
        staleView.latestOpenUtc = now - 3 * 3600;
        auto r5 = eng2.evaluateEvent(staleView, ev, now);
        check(!r5.empty() && r5[0].direction == EngineDirection::NO_TRADE,
              "event: stale candles => no trade");

        // Bias below threshold: warning only.
        EconomicEvent weak = ev;
        weak.goldBias = 0.05;
        auto r6 = eng2.evaluateEvent(v, weak, now);
        check(!r6.empty() && r6[0].direction == EngineDirection::NO_TRADE,
              "event: bias below threshold => no trade");

        // No event: silent.
        check(eng.evaluate(v).empty(), "event: no event => engine is silent");
    }

    std::printf("RESULT: %s (%d passed, %d failed)\n",
                g_fail == 0 ? "PASS" : "FAIL", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
